#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "chess/board.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;
namespace {
void require(bool ok, const char* message) {
  if (!ok) { std::cerr << "V4910_FAIL=" << message << '\n'; std::exit(1); }
}
const char* kGame0Gcf38 =
    "2bQ4/2p2p1k/1p2p3/5rpN/8/8/p1PP1PPK/Rq6 w - - 0 38";
const char* kGame0Gcf39 =
    "2bQ4/2p2p1k/1p2p3/5rpN/8/8/q1PP1PPK/8 w - - 0 39";
SearchResult analyze(const char* fen, int depth, EvalMode mode = EvalMode::CHAOS) {
  const auto board = Board::from_fen(fen);
  require(board.has_value(), "FEN legal");
  SearchLimits limits;
  limits.max_depth = depth;
  limits.eval_mode = mode;
  clear_transposition_table();
  clear_search_heuristics();
  return search(*board, limits);
}
void test_38_queen_threat_not_pawn_grab() {
  const auto board = Board::from_fen(kGame0Gcf38);
  require(board.has_value(), "38 FEN exists");
  require(parse_uci_move(*board, "a1a2").has_value() &&
          parse_uci_move(*board, "d8f8").has_value(), "game 38 legal root moves");
  // This is NOT a solved mate test. The live bot had chosen 38.Rxa2??, and
  // deeper external analysis shows a Qf8 king-attack continuation.
  const auto result = analyze(kGame0Gcf38, 11);
  std::cout << "GAME_0GCF_38=" << move_to_uci(result.best_move)
            << " depth=" << result.completed_depth
            << " reason=" << result.v4910_mate_threat_reason << '\n';
  require(move_to_uci(result.best_move) == "d8f8", "38 force Qf8 mating threat");
  require(result.v4910_mate_threat_selected &&
          result.v4910_mate_threat_reason == "hanging_rook_pawn_grab",
          "38 grounded rook-pawn-greed veto");
  require(result.v4910_mate_threat_candidates == 1,
          "38 reject Qf6? unrecouped queen capture");
}
void test_39_mate_threat_floor_proof() {
  const auto result = analyze(kGame0Gcf39, 10);
  std::cout << "GAME_0GCF_39=" << move_to_uci(result.best_move)
            << " depth=" << result.completed_depth
            << " reason=" << result.v4910_mate_threat_reason << '\n';
  require(move_to_uci(result.best_move) == "d8f8", "39 Qf8 instead of g4");
  require(result.v4910_mate_threat_selected &&
          result.v4910_mate_threat_reason == "score_floor_proven",
          "39 completed conservative search-floor proof");
  require(result.v4910_mate_threat_candidates == 1,
          "39 avoid hanging queen Qf6");
}
void test_39_objective_find_at_depth_11() {
  const auto result = analyze(kGame0Gcf39, 11);
  require(move_to_uci(result.best_move) == "d8f8", "39 deeper objective still Qf8");
  // If objective itself chose the threat, don't need special style override.
  require(!result.v4910_mate_threat_selected,
          "39 objective Qf8 does not need speculative override");
}
void test_engine_uci_identity() {
  std::vector<std::string> messages;
  UciEngine engine([&](const std::string& s) { messages.push_back(s); });
  engine.send_command("uci");
  bool found = false;
  for (const auto& item : messages)
    found |= item == "id name JJUGLE Chaos v4.9.11 Defensive Pressure Triage";
  require(found, "UCI v4.9.10 identity");
}
} // namespace
int main() {
  test_38_queen_threat_not_pawn_grab();
  test_39_mate_threat_floor_proof();
  test_39_objective_find_at_depth_11();
  test_engine_uci_identity();
  std::cout << "V4910_KING_THREAT_TRIAGE=PASS\n";
}
