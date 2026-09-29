#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "chess/eval.hpp"
#include "chess/stockfish14_nnue.hpp"

namespace {

constexpr const char* kRegressionFen =
    "r1bqkb1r/1p3ppp/p2ppn2/2n5/2BNP2P/2N1BQ2/PPP2PP1/R3K2R b KQkq - 2 9";

int fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return fail("usage: stockfish14_nnue_reference_test <network.nnue>");
  const std::filesystem::path path(argv[1]);
  if (!std::filesystem::exists(path)) return fail("network file is missing");

  auto board = hebichess::Board::from_fen(kRegressionFen);
  if (!board) return fail("regression FEN rejected by HebiChess");
  const int hce_before = hebichess::evaluate_hce(*board);
  if (hebichess::eval_mode_available(hebichess::EvalMode::Stockfish14NNUE))
    return fail("SF14 evaluator unexpectedly available before load");

  std::string error;
  const std::uint8_t malformed[] = {0x20, 0x2f, 0xf3, 0x7a};
  if (hebichess::load_stockfish14_nnue_network_bytes(
        malformed, sizeof(malformed), error))
    return fail("malformed network unexpectedly loaded");
  if (hebichess::stockfish14_nnue_network_available())
    return fail("failed network load left evaluator enabled");

  const auto started = std::chrono::steady_clock::now();
  if (!hebichess::load_stockfish14_nnue_network(path.string(), error))
    return fail("official network failed to load: " + error);
  const auto load_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  if (!hebichess::eval_mode_available(hebichess::EvalMode::Stockfish14NNUE))
    return fail("SF14 evaluator remains unavailable after valid load");
  if (hebichess::evaluate_hce(*board) != hce_before)
    return fail("SF14 load changed the HCE score");

  const auto first = hebichess::evaluate(*board, hebichess::EvalMode::Stockfish14NNUE);
  const auto repeated = hebichess::evaluate(*board, hebichess::EvalMode::Stockfish14NNUE);
  if (!first || first != repeated) return fail("repeated raw evaluation differs");

  const hebichess::Move probe{
    hebichess::Square::from_file_rank(3, 5),
    hebichess::Square::from_file_rank(3, 4),
    hebichess::PieceType::None,
    hebichess::MoveFlag::Normal};
  if (!hebichess::stockfish14_nnue_incremental_parity(*board, probe, error))
    return fail("SF14 incremental/full/unmake parity failed: " + error);

  std::cout << "network_bytes " << std::filesystem::file_size(path) << '\n'
            << "network_resident_bytes "
            << hebichess::stockfish14_nnue_network_storage_bytes() << '\n'
            << "network_load_ms " << load_ms << '\n'
            << "regression_raw_eval " << *first << '\n'
            << "hce_regression_eval " << hce_before << '\n'
            << "repeated_eval_determinism pass\n"
            << "incremental_vs_refresh_accumulator pass\n"
            << "make_unmake_accumulator pass\n"
            << "network_load_failure pass\n"
            << "network_load_success pass\n";
  return 0;
}
