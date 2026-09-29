#include "chess/stockfish_nnue.hpp"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>

#include "stockfish/src/attacks.h"
#include "stockfish/src/nnue/nnue_accumulator.h"
#include "stockfish/src/nnue/network.h"
#include "stockfish/src/position.h"

namespace hebichess {
namespace {

using StockfishNetwork = Stockfish::Eval::NNUE::Network;
using StockfishAccumulatorStack = Stockfish::Eval::NNUE::AccumulatorStack;
using StockfishAccumulatorCaches = Stockfish::Eval::NNUE::AccumulatorCaches;

struct Runtime {
  std::shared_mutex mutex;
  std::unique_ptr<StockfishNetwork> network;
  std::uint64_t generation{0};
};

Runtime& runtime() {
  static Runtime value;
  return value;
}

struct Counters {
  std::atomic<std::uint64_t> evaluation_calls{0};
  std::atomic<std::uint64_t> accumulator_transform_requests{0};
  std::atomic<std::uint64_t> failures{0};
};

Counters& counters() {
  static Counters value;
  return value;
}

void initialize_stockfish_tables() {
  static std::once_flag once;
  std::call_once(once, [] {
    Stockfish::Attacks::init();
    Stockfish::Position::init();
  });
}

struct ThreadScratch {
  std::uint64_t generation{0};
  std::unique_ptr<StockfishAccumulatorCaches> caches;
  StockfishAccumulatorStack stack;
};

ThreadScratch& thread_scratch() {
  thread_local ThreadScratch scratch;
  return scratch;
}

bool set_position(const Board& board, Stockfish::Position& position,
                  Stockfish::StateInfo& state, std::string& error) {
  const auto invalid = position.set(board.to_fen(), false, &state);
  if (!invalid) return true;
  error = invalid->what();
  return false;
}

bool same_accumulator(const Stockfish::Eval::NNUE::AccumulatorState& lhs,
                      const Stockfish::Eval::NNUE::AccumulatorState& rhs) {
  return lhs.accumulation == rhs.accumulation &&
         lhs.psqtAccumulation == rhs.psqtAccumulation &&
         lhs.computed == rhs.computed;
}

}  // namespace

bool load_stockfish_nnue_network(const std::string& path, std::string& error) {
  std::unique_lock lock(runtime().mutex);
  runtime().network.reset();
  ++runtime().generation;
  try {
    initialize_stockfish_tables();
    auto candidate = std::make_unique<StockfishNetwork>();
    Stockfish::Eval::NNUE::EvalFile eval_file;
    candidate->load_external(std::filesystem::path{}, std::filesystem::path(path), eval_file);
    if (!eval_file.current) {
      error = "Stockfish NNUE network failed Stockfish header/architecture validation";
      return false;
    }
    runtime().network = std::move(candidate);
    error.clear();
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  } catch (...) {
    error = "Stockfish NNUE network load failed";
    return false;
  }
}

bool load_stockfish_nnue_network_bytes(const std::uint8_t* bytes, std::size_t size,
                                       std::string& error) {
  std::unique_lock lock(runtime().mutex);
  runtime().network.reset();
  ++runtime().generation;
  try {
    initialize_stockfish_tables();
    auto candidate = std::make_unique<StockfishNetwork>();
    if (!candidate->load_from_memory(reinterpret_cast<const char*>(bytes), size)) {
      error = "Stockfish NNUE network failed Stockfish header/architecture validation";
      return false;
    }
    runtime().network = std::move(candidate);
    error.clear();
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  } catch (...) {
    error = "Stockfish NNUE network load failed";
    return false;
  }
}

bool stockfish_nnue_network_available() noexcept {
  std::shared_lock lock(runtime().mutex);
  return runtime().network != nullptr;
}

void clear_stockfish_nnue_network() noexcept {
  std::unique_lock lock(runtime().mutex);
  runtime().network.reset();
  ++runtime().generation;
}

std::size_t stockfish_nnue_network_storage_bytes() noexcept {
  return sizeof(StockfishNetwork);
}

std::optional<int> evaluate_stockfish_nnue(const Board& board) noexcept {
  counters().evaluation_calls.fetch_add(1, std::memory_order_relaxed);
  try {
    std::shared_lock lock(runtime().mutex);
    if (!runtime().network) return std::nullopt;
    initialize_stockfish_tables();

    auto& scratch = thread_scratch();
    if (!scratch.caches || scratch.generation != runtime().generation) {
      scratch.caches = std::make_unique<StockfishAccumulatorCaches>(*runtime().network);
      scratch.generation = runtime().generation;
    }
    scratch.stack.reset();

    Stockfish::Position position;
    Stockfish::StateInfo state{};
    std::string error;
    if (!set_position(board, position, state, error)) {
      counters().failures.fetch_add(1, std::memory_order_relaxed);
      return std::nullopt;
    }

    counters().accumulator_transform_requests.fetch_add(1, std::memory_order_relaxed);
    const auto [psqt, positional] = runtime().network->evaluate(
        position, scratch.stack, *scratch.caches);
    return static_cast<int>(psqt + positional);
  } catch (...) {
    counters().failures.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
  }
}

StockfishNnueStats stockfish_nnue_stats() noexcept {
  return {counters().evaluation_calls.load(std::memory_order_relaxed),
          counters().accumulator_transform_requests.load(std::memory_order_relaxed),
          counters().failures.load(std::memory_order_relaxed)};
}

void reset_stockfish_nnue_stats() noexcept {
  counters().evaluation_calls.store(0, std::memory_order_relaxed);
  counters().accumulator_transform_requests.store(0, std::memory_order_relaxed);
  counters().failures.store(0, std::memory_order_relaxed);
}

bool stockfish_nnue_incremental_parity(const Board& board, const Move& move,
                                       std::string& error) noexcept {
  if (move.is_promotion() || move.flag == MoveFlag::CastleKingSide ||
      move.flag == MoveFlag::CastleQueenSide || move.flag == MoveFlag::EnPassant) {
    error = "incremental parity probe requires a non-promotion, non-castling move";
    return false;
  }
  try {
    std::shared_lock lock(runtime().mutex);
    if (!runtime().network) {
      error = "Stockfish NNUE network is not loaded";
      return false;
    }
    initialize_stockfish_tables();
    Stockfish::Position position;
    Stockfish::StateInfo root_state{}, child_state{};
    if (!set_position(board, position, root_state, error)) return false;

    auto caches = std::make_unique<StockfishAccumulatorCaches>(*runtime().network);
    auto incremental = std::make_unique<StockfishAccumulatorStack>();
    incremental->reset();
    runtime().network->evaluate(position, *incremental, *caches);
    const auto root_accumulator = incremental->latest();

    const Stockfish::Move sf_move(static_cast<Stockfish::Square>(move.from.index()),
                                  static_cast<Stockfish::Square>(move.to.index()));
    auto& dirty = incremental->push();
    position.do_move(sf_move, child_state, position.gives_check(sf_move), dirty, nullptr, nullptr);
    runtime().network->evaluate(position, *incremental, *caches);
    const auto incremental_child = incremental->latest();

    auto refreshed = std::make_unique<StockfishAccumulatorStack>();
    refreshed->reset();
    runtime().network->evaluate(position, *refreshed, *caches);
    if (!same_accumulator(incremental_child, refreshed->latest())) {
      error = "Stockfish incremental accumulator differs from full refresh after make_move";
      return false;
    }

    position.undo_move(sf_move);
    incremental->pop();
    runtime().network->evaluate(position, *incremental, *caches);
    if (!same_accumulator(root_accumulator, incremental->latest())) {
      error = "Stockfish accumulator differs from parent after unmake_move";
      return false;
    }
    error.clear();
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  } catch (...) {
    error = "Stockfish incremental accumulator parity failed";
    return false;
  }
}

}  // namespace hebichess
