#include <cassert>
#include <chrono>
#include <string>
#include <vector>

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
      "info string eval material 900",
      "info string eval pst -4",
      "info string eval mobility 44",
      "info string eval pawns 0",
      "info string eval passed_pawns 0",
      "info string eval bishop_pair 0",
      "info string eval rook_activity 0",
      "info string eval king_safety 5",
      "info string eval king_attack 1",
      "info string eval space 9",
      "info string eval threats 0",
      "info string eval initiative 10",
      "info string eval total 965"}));
}
}

int main() {
  test_move_parsing();
  test_serialization_and_timeout_integrity();
  test_eval_breakdown_uci_output();
}
