#include <iostream>
#include <stdexcept>
#include <string>

#include "chess/nnue.hpp"
#include "chess/search.hpp"

using namespace hebichess;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_false_cutoff_is_rejected() {
  const auto board = Board::from_fen(
      "8/2b5/2p3B1/p6p/K7/2k3P1/7P/8 b - - 0 3");
  require(board.has_value(), "false-cutoff fixture must parse");
  const NmpCandidateGWindowResult result =
      search_nmp_candidate_g_window_for_test(*board, 3, 727, 728, 4,
                                             EvalMode::NNUE);
  require(result.verification_attempts == 1,
          "candidate G must verify the focused low-material fail-high");
  require(result.rejected_verifications == 1 &&
              result.confirmed_verifications == 0,
          "candidate G must reject the focused false cutoff");
  require(result.score < 728,
          "rejected false cutoff must continue and remain below beta");
}

void test_legitimate_cutoff_is_confirmed() {
  const auto board = Board::from_fen(
      "8/2b2p2/2p3p1/p6p/K2k4/1B4P1/7P/8 w - - 1 2");
  require(board.has_value(), "legitimate-cutoff fixture must parse");
  const NmpCandidateGWindowResult result =
      search_nmp_candidate_g_window_for_test(*board, 3, -795, -725, 1,
                                             EvalMode::NNUE);
  require(result.verification_attempts == 1,
          "candidate G must verify the legitimate low-material fail-high");
  require(result.confirmed_verifications == 1 &&
              result.rejected_verifications == 0,
          "candidate G must preserve a verified legitimate cutoff");
  require(result.score >= -725,
          "verified legitimate cutoff must still reach beta");
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    require(argc == 3 && std::string(argv[1]) == "--network",
            "usage: HebiChessNmpCandidateGTest --network model.hebinnue");
    std::string error;
    require(load_nnue_network(argv[2], error), error.c_str());
    test_false_cutoff_is_rejected();
    test_legitimate_cutoff_is_confirmed();
  } catch (const std::exception& error) {
    std::cerr << "NMP candidate G regression failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
