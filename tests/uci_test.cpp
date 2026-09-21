#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "chess/opening_book.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;

namespace {
Board position(const char* fen) {
  const auto board = Board::from_fen(fen);
  assert(board);
  return *board;
}

void put16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void put64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
std::vector<std::uint8_t> book_fixture(const Board& board, Move candidate,
                                       std::uint16_t max_ply = 30) {
  std::vector<std::uint8_t> bytes{'H', 'E', 'B', 'I', 'B', 'O', 'O', 'K'};
  put32(bytes, 1); put32(bytes, 1); put32(bytes, 1);
  put16(bytes, max_ply); put16(bytes, 1);
  put64(bytes, opening_book_key(board)); put32(bytes, 0); put16(bytes, 1); put16(bytes, 0);
  put16(bytes, OpeningBook::pack_move(candidate)); put16(bytes, 0); put32(bytes, 1);
  return bytes;
}

void test_uci_opening_book_path() {
  const Board initial = Board::initial();
  const Move e4 = *parse_uci_move(initial, "e2e4");
  std::vector<std::string> output;
  UciEngine engine([&output](const std::string& line) { output.push_back(line); });
  assert(engine.load_opening_book_bytes(book_fixture(initial, e4)));
  assert(engine.opening_book_available());
  engine.send_command("position startpos");
  engine.send_command("go depth 1");
  bool searched = false;
  for (const auto& line : output) searched |= line.rfind("info depth ", 0) == 0;
  assert(searched);  // OwnBook defaults to false.

  output.clear();
  engine.send_command("setoption name BookSeed value 7");
  engine.send_command("setoption name OwnBook value true");
  engine.send_command("go depth 64");
  assert((output == std::vector<std::string>{"info string BookSeed 7", "info string OwnBook true",
                                             "info string book hit", "bestmove e2e4"}));

  output.clear();
  engine.send_command("position fen 4k3/8/8/8/8/8/8/4K2R w - - 0 16");
  engine.send_command("go depth 1");
  searched = false;
  for (const auto& line : output) searched |= line.rfind("info depth ", 0) == 0;
  assert(searched);  // Current ply is 30, so the book is disabled.

  output.clear();
  engine.send_command("setoption name BookFile value");
  assert(!engine.opening_book_available());
}

void test_move_parsing() {
  {
    const Board board = Board::initial();
    assert(parse_uci_move(board, "e2e4"));
    assert(parse_uci_move(board, "g1f3"));
  }
  {
    Board board = Board::initial();
    board.make_move(*parse_uci_move(board, "e2e4"));
    board.make_move(*parse_uci_move(board, "d7d5"));
    assert(parse_uci_move(board, "e4d5")->flag == MoveFlag::Capture);
  }
  assert(parse_uci_move(position("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), "e1g1")->flag == MoveFlag::CastleKingSide);
  assert(parse_uci_move(position("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"), "e5d6")->flag == MoveFlag::EnPassant);
  assert(parse_uci_move(position("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"), "a7a8q")->flag == MoveFlag::Promotion);
  assert(parse_uci_move(position("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1"), "a7b8n")->flag == MoveFlag::PromotionCapture);
}

void test_serialization_and_timeout_integrity() {
  const Board board = Board::initial();
  const Move move = *parse_uci_move(board, "e2e4");
  assert(move_to_uci(move) == "e2e4");
  const Move promotion = *parse_uci_move(position("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"), "a7a8q");
  assert(move_to_uci(promotion) == "a7a8q");

  const std::string before = board.to_fen();
  SearchLimits limits;
  limits.max_depth = 64;
  limits.has_deadline = true;
  limits.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
  const SearchResult result = search(board, limits);
  assert(board.to_fen() == before);
  bool legal = false;
  for (const Move& candidate : generate_legal_moves(board)) legal |= candidate == result.best_move;
  assert(legal);
  assert(result.completed_depth > 0);
  assert(result.completed_depth < limits.max_depth);
  // The final result is the last wholly completed iteration: it must contain
  // every root move, never the partially searched iteration that hit timeout.
  assert(result.root_moves.size() == generate_legal_moves(board).size());
}

void test_eval_breakdown_uci_output() {
  std::vector<std::string> output;
  UciEngine engine([&output](const std::string& line) { output.push_back(line); });
  engine.send_command("position fen 4k3/8/8/8/8/8/4Q3/4K3 w - - 0 1");
  engine.send_command("eval");
  assert((output == std::vector<std::string>{
      "material 900",
      "pst -4",
      "mobility 44",
      "pawns 0",
      "passed_pawns 0",
      "bishop_pair 0",
      "rook_activity 0",
      "king_safety 5",
      "king_attack 1",
      "space 9",
      "threats 0",
      "initiative 10",
      "total 965"}));
}
}

int main() {
  test_move_parsing();
  test_serialization_and_timeout_integrity();
  test_eval_breakdown_uci_output();
  test_uci_opening_book_path();
}
