#include "chess/stockfish14_nnue.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <streambuf>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include "stockfish14/src/bitboard.h"
#include "stockfish14/src/position.h"
#include "stockfish14/src/nnue/evaluate_nnue.h"
#include "stockfish14/src/psqt.h"
#include "stockfish14/src/uci.h"

namespace Stockfish14::Eval {
bool useNNUE = true;
std::string eval_file_loaded;
}  // namespace Stockfish14::Eval

namespace Stockfish14 {
UCI::OptionsMap Options;
}  // namespace Stockfish14

namespace Stockfish14::UCI {
std::string square(Square sq) {
  std::string result = "a1";
  result[0] += file_of(sq);
  result[1] += rank_of(sq);
  return result;
}
}  // namespace Stockfish14::UCI

namespace Stockfish14 {
void prefetch(void*) {}
void* std_aligned_alloc(std::size_t alignment, std::size_t size) {
#if defined(_MSC_VER)
  return _aligned_malloc(size, alignment);
#else
  return std::aligned_alloc(alignment, size);
#endif
}
void std_aligned_free(void* ptr) {
#if defined(_MSC_VER)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}
void* aligned_large_pages_alloc(std::size_t size) {
#if defined(_MSC_VER)
  return _aligned_malloc(size, 4096);
#else
  const std::size_t rounded = (size + 4095) & ~std::size_t(4095);
  return std::aligned_alloc(4096, rounded);
#endif
}
void aligned_large_pages_free(void* ptr) { std_aligned_free(ptr); }
std::ostream& operator<<(std::ostream& out, SyncCout) { return out; }
}  // namespace Stockfish14

namespace Stockfish14::Eval::NNUE {
extern LargePagePtr<FeatureTransformer> featureTransformer;
extern AlignedPtr<Network> network[LayerStacks];
void clear_eval() {
  featureTransformer.reset();
  for (auto& stack : network) stack.reset();
}
}  // namespace Stockfish14::Eval::NNUE

namespace hebichess {
namespace {

using StockfishFeatureTransformer = Stockfish14::Eval::NNUE::FeatureTransformer;
using StockfishPosition = Stockfish14::Position;
using StockfishStateInfo = Stockfish14::StateInfo;

class ReadOnlyBuffer final : public std::streambuf {
 public:
  ReadOnlyBuffer(const std::uint8_t* bytes, std::size_t size) {
    auto* begin = const_cast<char*>(reinterpret_cast<const char*>(bytes));
    setg(begin, begin, begin + size);
  }
  std::size_t remaining() const noexcept {
    return static_cast<std::size_t>(egptr() - gptr());
  }
};

struct Runtime {
  std::shared_mutex mutex;
  std::size_t network_bytes{0};
  bool loaded{false};
};

Runtime& runtime() {
  static Runtime value;
  return value;
}

struct Counters {
  std::atomic<std::uint64_t> evaluations{0};
  std::atomic<std::uint64_t> accumulator_updates{0};
  std::atomic<std::uint64_t> failures{0};
};

Counters& counters() {
  static Counters value;
  return value;
}

void initialize_stockfish14_tables() {
  static std::once_flag once;
  std::call_once(once, [] {
    Stockfish14::Bitboards::init();
    Stockfish14::Position::init();
    Stockfish14::PSQT::init();
  });
}

bool load_from_bytes(Runtime& target, const std::uint8_t* bytes,
                     std::size_t size, std::string& error) {
  if (bytes == nullptr || size < 12) {
    error = "network bytes are empty or truncated";
    return false;
  }
  ReadOnlyBuffer buffer(bytes, size);
  std::istream stream(&buffer);
  Stockfish14::Eval::NNUE::clear_eval();
  if (!Stockfish14::Eval::NNUE::load_eval("nn-3475407dc199.nnue", stream)) {
    error = "Stockfish 14 upstream parser rejected network version, architecture, or parameters";
    Stockfish14::Eval::NNUE::clear_eval();
    return false;
  }
  (void)buffer;
  target.network_bytes = size;
  target.loaded = true;
  return true;
}

bool set_position(const Board& board, StockfishPosition& position,
                  StockfishStateInfo& state) {
  position.set(board.to_fen(), false, &state, nullptr);
  return true;
}

std::optional<int> evaluate_position(Runtime& state, StockfishPosition& position) {
  if (!state.loaded) return std::nullopt;
  counters().accumulator_updates.fetch_add(2, std::memory_order_relaxed);
  return Stockfish14::Eval::NNUE::evaluate(position, false);
}

bool same_accumulator(const Stockfish14::Eval::NNUE::Accumulator& lhs,
                      const Stockfish14::Eval::NNUE::Accumulator& rhs) {
  return std::memcmp(lhs.accumulation, rhs.accumulation,
                     sizeof(lhs.accumulation)) == 0 &&
         std::memcmp(lhs.psqtAccumulation, rhs.psqtAccumulation,
                     sizeof(lhs.psqtAccumulation)) == 0 &&
         std::memcmp(lhs.computed, rhs.computed, sizeof(lhs.computed)) == 0;
}

}  // namespace

bool load_stockfish14_nnue_network_bytes(const std::uint8_t* bytes,
                                         std::size_t size, std::string& error) {
  std::unique_lock lock(runtime().mutex);
  Stockfish14::Eval::NNUE::clear_eval();
  runtime().loaded = false;
  runtime().network_bytes = 0;
  try {
    initialize_stockfish14_tables();
    if (!load_from_bytes(runtime(), bytes, size, error)) return false;
    error.clear();
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  } catch (...) {
    error = "Stockfish 14 NNUE load failed";
    return false;
  }
}

bool load_stockfish14_nnue_network(const std::string& path, std::string& error) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "cannot open Stockfish 14 NNUE network: " + path;
    return false;
  }
  const auto end = file.tellg();
  if (end <= 0) {
    error = "Stockfish 14 NNUE network is empty";
    return false;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(bytes.data()), end);
  if (!file) {
    error = "cannot read Stockfish 14 NNUE network: " + path;
    return false;
  }
  return load_stockfish14_nnue_network_bytes(bytes.data(), bytes.size(), error);
}

bool stockfish14_nnue_network_available() noexcept {
  std::shared_lock lock(runtime().mutex);
  return runtime().loaded;
}

void clear_stockfish14_nnue_network() noexcept {
  std::unique_lock lock(runtime().mutex);
  Stockfish14::Eval::NNUE::clear_eval();
  runtime().loaded = false;
  runtime().network_bytes = 0;
}

std::size_t stockfish14_nnue_network_storage_bytes() noexcept {
  std::shared_lock lock(runtime().mutex);
  return runtime().loaded ? sizeof(StockfishFeatureTransformer) +
      Stockfish14::Eval::NNUE::LayerStacks * sizeof(Stockfish14::Eval::NNUE::Network) : 0;
}

std::optional<int> evaluate_stockfish14_nnue(const Board& board) noexcept {
  counters().evaluations.fetch_add(1, std::memory_order_relaxed);
  try {
    std::shared_lock lock(runtime().mutex);
    if (!runtime().loaded) return std::nullopt;
    initialize_stockfish14_tables();
    StockfishPosition position;
    StockfishStateInfo state{};
    set_position(board, position, state);
    return evaluate_position(runtime(), position);
  } catch (...) {
    counters().failures.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
  }
}

Stockfish14NnueStats stockfish14_nnue_stats() noexcept {
  return {counters().evaluations.load(std::memory_order_relaxed),
          counters().accumulator_updates.load(std::memory_order_relaxed),
          counters().failures.load(std::memory_order_relaxed)};
}

void reset_stockfish14_nnue_stats() noexcept {
  counters().evaluations.store(0, std::memory_order_relaxed);
  counters().accumulator_updates.store(0, std::memory_order_relaxed);
  counters().failures.store(0, std::memory_order_relaxed);
}

bool stockfish14_nnue_incremental_parity(const Board& board, const Move& move,
                                         std::string& error) noexcept {
  if (move.is_promotion() || move.flag == MoveFlag::CastleKingSide ||
      move.flag == MoveFlag::CastleQueenSide || move.flag == MoveFlag::EnPassant) {
    error = "incremental parity probe requires a non-special move";
    return false;
  }
  try {
    std::shared_lock lock(runtime().mutex);
    if (!runtime().loaded) {
      error = "Stockfish 14 NNUE network is not loaded";
      return false;
    }
    StockfishPosition position;
    StockfishStateInfo root{}, child{};
    set_position(board, position, root);
    if (!evaluate_position(runtime(), position)) {
      error = "Stockfish 14 root evaluation failed";
      return false;
    }
    const auto root_accumulator = root.accumulator;
    const Stockfish14::Move sf_move = Stockfish14::make_move(
      static_cast<Stockfish14::Square>(move.from.index()),
      static_cast<Stockfish14::Square>(move.to.index()));
    position.do_move(sf_move, child, position.gives_check(sf_move));
    if (!evaluate_position(runtime(), position)) {
      error = "Stockfish 14 child evaluation failed";
      return false;
    }
    const auto incremental_child = child.accumulator;

    StockfishPosition refreshed_position;
    StockfishStateInfo refreshed_state{};
    set_position(Board::from_fen(position.fen()).value(), refreshed_position,
                 refreshed_state);
    if (!evaluate_position(runtime(), refreshed_position) ||
        !same_accumulator(incremental_child, refreshed_state.accumulator)) {
      error = "Stockfish 14 incremental accumulator differs from full refresh";
      return false;
    }

    position.undo_move(sf_move);
    if (!evaluate_position(runtime(), position) ||
        !same_accumulator(root_accumulator, root.accumulator)) {
      error = "Stockfish 14 make/unmake accumulator parity failed";
      return false;
    }
    error.clear();
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  } catch (...) {
    error = "Stockfish 14 NNUE accumulator parity failed";
    return false;
  }
}

}  // namespace hebichess
