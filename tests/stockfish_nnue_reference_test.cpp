#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/stockfish_nnue.hpp"
#include "chess/uci_engine.hpp"
#include "chess/uci.hpp"

namespace {

constexpr const char* kRegressionFen =
    "r1bqkb1r/1p3ppp/p2ppn2/2n5/2BNP2P/2N1BQ2/PPP2PP1/R3K2R b KQkq - 2 9";

int fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("usage: stockfish_nnue_reference_test <network.nnue>");
  const std::filesystem::path network_path(argv[1]);
  if (!std::filesystem::exists(network_path)) return fail("network file is missing");

  auto regression = hebichess::Board::from_fen(kRegressionFen);
  if (!regression) return fail("regression FEN rejected by HebiChess");
  const int hce_before = hebichess::evaluate_hce(*regression);
  if (hebichess::eval_mode_available(hebichess::EvalMode::StockfishNNUE))
    return fail("Stockfish evaluator available before a network load");

  std::vector<std::string> protocol_lines;
  hebichess::UciEngine protocol([&](const std::string& line) { protocol_lines.push_back(line); });
  protocol.send_command("uci");
  const auto has_line = [&](const std::string& prefix) {
    return std::any_of(protocol_lines.begin(), protocol_lines.end(), [&](const std::string& line) {
      return line.starts_with(prefix);
    });
  };
  if (!has_line("option name EvalMode type combo default HCE"))
    return fail("experimental UCI mode no longer defaults to HCE");
  if (!has_line("option name StockfishEvalFile type string"))
    return fail("explicit Stockfish NNUE UCI loader option is missing");

  const std::uint8_t invalid_network[] = {0, 1, 2, 3, 4, 5};
  std::string error;
  if (hebichess::load_stockfish_nnue_network_bytes(
          invalid_network, sizeof(invalid_network), error))
    return fail("invalid network bytes unexpectedly loaded");
  if (hebichess::stockfish_nnue_network_available())
    return fail("invalid network bytes left the evaluator enabled");

  if (hebichess::load_stockfish_nnue_network(network_path.string() + ".missing", error))
    return fail("truncated/nonexistent path unexpectedly loaded");
  if (hebichess::stockfish_nnue_network_available())
    return fail("failed path load left the evaluator enabled");

  const auto load_started = std::chrono::steady_clock::now();
  if (!hebichess::load_stockfish_nnue_network(network_path.string(), error))
    return fail("official network failed to load: " + error);
  const auto load_elapsed_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - load_started).count();
  if (!hebichess::eval_mode_available(hebichess::EvalMode::StockfishNNUE))
    return fail("Stockfish evaluator remains unavailable after load");
  if (hebichess::evaluate_hce(*regression) != hce_before)
    return fail("loading Stockfish NNUE changed the HCE result");

  const auto first = hebichess::evaluate(*regression, hebichess::EvalMode::StockfishNNUE);
  const auto repeated = hebichess::evaluate(*regression, hebichess::EvalMode::StockfishNNUE);
  if (!first || first != repeated) return fail("repeated Stockfish eval is not deterministic");
  if (*first != -15) return fail("regression score differs from scalar/WASM reference: " +
                                 std::to_string(*first));

  const auto moves = hebichess::generate_legal_moves(*regression);
  const auto move_it = std::find_if(moves.begin(), moves.end(), [](const hebichess::Move& move) {
    return hebichess::move_to_uci(move) == "c5d3";
  });
  if (move_it == moves.end()) return fail("regression make/unmake probe move is not legal");
  if (!hebichess::stockfish_nnue_incremental_parity(*regression, *move_it, error))
    return fail("upstream accumulator incremental/full parity failed: " + error);

  const std::string original_fen = regression->to_fen();
  const int root_score = *first;
  const auto undo = regression->make_move(*move_it);
  const auto child_score = hebichess::evaluate(*regression, hebichess::EvalMode::StockfishNNUE);
  if (!child_score) return fail("Stockfish eval failed after make_move");
  regression->unmake_move(*move_it, undo);
  const auto restored_score = hebichess::evaluate(*regression, hebichess::EvalMode::StockfishNNUE);
  if (regression->to_fen() != original_fen || restored_score != root_score)
    return fail("Stockfish eval parity failed after HebiChess make/unmake");

  const auto start = hebichess::Board::initial();
  const auto start_eval = hebichess::evaluate(start, hebichess::EvalMode::StockfishNNUE);
  if (!start_eval || start_eval != hebichess::evaluate(start, hebichess::EvalMode::StockfishNNUE))
    return fail("start-position Stockfish eval is not deterministic");

  std::cout << "network_bytes " << std::filesystem::file_size(network_path) << '\n'
            << "network_storage_bytes " << hebichess::stockfish_nnue_network_storage_bytes() << '\n'
            << "network_load_ms " << load_elapsed_ms << '\n'
            << "regression_eval_cp " << *first << '\n'
            << "startpos_eval_cp " << *start_eval << '\n'
            << "hce_regression_cp " << hce_before << '\n'
            << "incremental_accumulator_parity pass\n"
            << "make_unmake_eval_parity pass\n"
            << "network_load_failure pass\n"
            << "network_load_success pass\n";
  return 0;
}
