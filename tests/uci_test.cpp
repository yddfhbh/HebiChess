#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "chess/opening_book.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

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
std::vector<std::uint8_t> book_fixture(
    const Board& board, std::initializer_list<std::pair<Move, std::uint32_t>> candidates,
    std::uint16_t max_ply = 30) {
  std::vector<std::uint8_t> bytes{'H', 'E', 'B', 'I', 'B', 'O', 'O', 'K'};
  put32(bytes, 1); put32(bytes, 1); put32(bytes, static_cast<std::uint32_t>(candidates.size()));
  put16(bytes, max_ply); put16(bytes, static_cast<std::uint16_t>(candidates.size()));
  put64(bytes, opening_book_key(board)); put32(bytes, 0);
  put16(bytes, static_cast<std::uint16_t>(candidates.size())); put16(bytes, 0);
  for (const auto& [candidate, weight] : candidates) {
    put16(bytes, OpeningBook::pack_move(candidate)); put16(bytes, 0); put32(bytes, weight);
  }
  return bytes;
}

bool has_line(const std::vector<std::string>& output, const std::string& expected) {
  for (const auto& line : output) if (line == expected) return true;
  return false;
}

std::size_t line_index(const std::vector<std::string>& output, const std::string& expected) {
  for (std::size_t i = 0; i < output.size(); ++i) {
    if (output[i] == expected) return i;
  }
  return output.size();
}

void require_bounded_search(const std::vector<std::string>& output) {
  bool searched = false;
  bool bestmove = false;
  for (const auto& line : output) {
    searched |= line.rfind("info depth ", 0) == 0;
    bestmove |= line.rfind("bestmove ", 0) == 0;
  }
  require(searched, "bounded fallback did not search");
  require(bestmove, "bounded fallback did not emit bestmove");
}

std::string book_bestmove(const Board& board, const std::vector<std::uint8_t>& bytes,
                          const char* seed) {
  std::vector<std::string> output;
  UciEngine engine([&output](const std::string& line) { output.push_back(line); });
  require(engine.load_opening_book_bytes(bytes), "opening book fixture failed to load");
  engine.send_command(std::string("setoption name BookSeed value ") + seed);
  engine.send_command("setoption name OwnBook value true");
  engine.send_command("position fen " + board.to_fen());
  engine.send_command("go movetime 50");
  require(has_line(output, "info string book hit"), "book fixture did not hit");
  require(!std::any_of(output.begin(), output.end(), [](const std::string& line) {
    return line.rfind("info depth ", 0) == 0;
  }), "book hit unexpectedly searched");
  for (const auto& line : output) {
    if (line.rfind("bestmove ", 0) == 0) return line.substr(9);
  }
  throw std::runtime_error("book hit did not emit bestmove");
}

std::vector<std::string> book_output(const Board& board, const std::vector<std::uint8_t>& bytes,
                                     const char* seed) {
  std::vector<std::string> output;
  UciEngine engine([&output](const std::string& line) { output.push_back(line); });
  require(engine.load_opening_book_bytes(bytes), "opening book fixture failed to load");
  engine.send_command(std::string("setoption name BookSeed value ") + seed);
  engine.send_command("setoption name OwnBook value true");
  engine.send_command("position fen " + board.to_fen());
  engine.send_command("go movetime 50");
  require(has_line(output, "info string book hit"), "book fixture did not hit");
  require(!std::any_of(output.begin(), output.end(), [](const std::string& line) {
    return line.rfind("info depth ", 0) == 0;
  }), "book hit unexpectedly searched");
  return output;
}

void test_uci_opening_book_path() {
  const Board initial = Board::initial();
  const Move e4 = *parse_uci_move(initial, "e2e4");
  const Move d4 = *parse_uci_move(initial, "d2d4");

  // OwnBook defaults to false even when a valid book is loaded.
  {
    std::vector<std::string> output;
    UciEngine engine([&output](const std::string& line) { output.push_back(line); });
    const bool loaded = engine.load_opening_book_bytes(book_fixture(initial, {{e4, 1}}));
    require(loaded, "opening book fixture failed to load");
    require(engine.opening_book_available(), "opening book should be available");
    engine.send_command("position startpos");
    engine.send_command("go movetime 50");
    require(!has_line(output, "info string book hit"), "OwnBook=false unexpectedly used book");
    require_bounded_search(output);
  }

  // A book hit must finish immediately and never enter the search loop.
  {
    std::vector<std::string> output;
    UciEngine engine([&output](const std::string& line) { output.push_back(line); });
    const bool loaded = engine.load_opening_book_bytes(book_fixture(initial, {{e4, 1}}));
    require(loaded, "opening book fixture failed to load");
    engine.send_command("setoption name BookSeed value 7");
    engine.send_command("setoption name OwnBook value true");
    engine.send_command("position startpos");
    engine.send_command("go movetime 50");
    require(has_line(output, "info string book hit"), "book hit was not reported");
    require(has_line(output, "bestmove e2e4"), "book returned unexpected move");
    Board post_move = initial;
    post_move.make_move(e4);
    const auto post_move_score = evaluate(post_move, EvalMode::HCE);
    require(post_move_score.has_value(), "HCE book evaluation was unavailable");
    const std::string book_eval = "info score cp " + std::to_string(-*post_move_score) +
                                  " nodes 0 string book_eval";
    require(has_line(output, book_eval),
            "book evaluation was not reported");
    require(line_index(output, book_eval) < line_index(output, "info string book hit"),
            "book evaluation arrived after book hit");
    require(line_index(output, "info string book hit") < line_index(output, "bestmove e2e4"),
            "book hit arrived after bestmove");
    require(!std::any_of(output.begin(), output.end(), [](const std::string& line) {
      return line.rfind("info depth ", 0) == 0;
    }), "book hit unexpectedly searched");
  }

  // A missing position and a malformed book both take the bounded fallback path.
  {
    std::vector<std::string> output;
    UciEngine engine([&output](const std::string& line) { output.push_back(line); });
    const bool loaded = engine.load_opening_book_bytes(book_fixture(initial, {{e4, 1}}));
    require(loaded, "opening book fixture failed to load");
    engine.send_command("setoption name OwnBook value true");
    engine.send_command("position fen 4k3/8/8/8/8/8/8/4K2R w - - 0 1");
    engine.send_command("go movetime 50");
    require(!has_line(output, "info string book hit"), "explicit miss unexpectedly hit book");
    require_bounded_search(output);

    auto malformed = book_fixture(initial, {{e4, 1}});
    malformed[0] = 'X';
    require(!engine.load_opening_book_bytes(malformed), "malformed book loaded");
    require(!engine.opening_book_available(), "malformed book remained available");
    output.clear();
    engine.send_command("position startpos");
    engine.send_command("go movetime 50");
    require_bounded_search(output);
  }

  // Ply 29 is eligible; ply 30 is outside the configured book range.
  {
    const Board ply29 = position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 15");
    const Move e5 = *parse_uci_move(ply29, "e7e5");
    require(book_bestmove(ply29, book_fixture(ply29, {{e5, 1}}), "0") == "e7e5",
            "ply 29 book hit returned unexpected move");
    const auto output_book = book_output(ply29, book_fixture(ply29, {{e5, 1}}), "0");
    Board post_move = ply29;
    post_move.make_move(e5);
    const auto post_move_score = evaluate(post_move, EvalMode::HCE);
    require(post_move_score.has_value(), "HCE book evaluation was unavailable");
    require(*post_move_score != 0, "black book perspective fixture was not discriminating");
    const auto expected = -*post_move_score;
    require(has_line(output_book, "info score cp " + std::to_string(expected) +
                              " nodes 0 string book_eval"),
            "black book evaluation had the wrong root perspective");

    const Board ply30 = position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 16");
    std::vector<std::string> output;
    UciEngine engine([&output](const std::string& line) { output.push_back(line); });
    const bool loaded = engine.load_opening_book_bytes(book_fixture(ply30, {{e4, 1}}));
    require(loaded, "opening book fixture failed to load");
    engine.send_command("setoption name OwnBook value true");
    engine.send_command("position fen " + ply30.to_fen());
    engine.send_command("go movetime 50");
    require(!has_line(output, "info string book hit"), "ply 30 unexpectedly used book");
    require_bounded_search(output);
  }

  // Fixed seeds are reproducible, and two precomputed seeds select both candidates.
  const auto candidates = book_fixture(initial, {{e4, 1}, {d4, 1}});
  require(book_bestmove(initial, candidates, "0") == book_bestmove(initial, candidates, "0"),
          "fixed BookSeed was not deterministic");
  require(book_bestmove(initial, candidates, "0") == "d2d4", "seed 0 selected unexpected move");
  require(book_bestmove(initial, candidates, "2") == "e2e4", "seed 2 selected unexpected move");

  {
    UciEngine engine([](const std::string&) {});
    const bool loaded = engine.load_opening_book_bytes(book_fixture(initial, {{e4, 1}}));
    require(loaded, "opening book fixture failed to load");
    require(engine.opening_book_available(), "opening book should be available");
    engine.clear_opening_book();
    require(!engine.opening_book_available(), "clear did not make book unavailable");
  }
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
