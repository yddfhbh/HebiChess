#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/nnue_features.hpp"

using namespace hebichess;

namespace {

constexpr std::uint32_t kHidden1 = 32;
constexpr std::uint32_t kHidden2 = 32;
constexpr std::uint64_t kParameterCount =
    static_cast<std::uint64_t>(kNnueInputDimensions) * kNnueAccumulatorDimensions +
    kNnueAccumulatorDimensions + static_cast<std::uint64_t>(kHidden1) *
    2 * kNnueAccumulatorDimensions + kHidden1 +
    static_cast<std::uint64_t>(kHidden2) * kHidden1 + kHidden2 + kHidden2 + 1;

#pragma pack(push, 1)
struct HeaderV3 {
  char magic[8];
  std::uint32_t version, feature_abi, input_dimensions, accumulator_dimensions;
  std::uint32_t hidden1_dimensions, hidden2_dimensions, output_dimensions;
  std::uint32_t scalar_type, endian_marker, final_hidden_activation;
  float output_scale;
  std::uint64_t parameter_count, checksum;
};
#pragma pack(pop)
static_assert(sizeof(HeaderV3) == 68);

struct Measurements {
  std::size_t targeted_cases{0};
  std::size_t random_transitions{0};
  float accumulator_max_abs_diff{0.0F};
  float eval_max_abs_diff_cp{0.0F};
  float composed_max_abs_diff_cp{0.0F};
};

void require(bool condition, const std::string& message) {
  if (condition) return;
  std::cerr << "NNUE incremental accumulator test failure: " << message << '\n';
  std::abort();
}

std::uint64_t fnv1a_bytes(const std::uint8_t* bytes, std::size_t size) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

float next_value(std::uint32_t& state, float limit) {
  state = state * 1664525U + 1013904223U;
  const float unit = static_cast<float>(state >> 8) / static_cast<float>(1U << 24);
  return (unit * 2.0F - 1.0F) * limit;
}

std::vector<std::uint8_t> deterministic_network() {
  std::vector<float> parameters;
  parameters.reserve(static_cast<std::size_t>(kParameterCount));
  std::uint32_t state = 0x6B1D2A9FU;
  const auto append = [&](std::size_t count, float limit) {
    for (std::size_t i = 0; i < count; ++i) parameters.push_back(next_value(state, limit));
  };
  append(kNnueInputDimensions * kNnueAccumulatorDimensions, 0.001F);
  for (std::size_t i = 0; i < kNnueAccumulatorDimensions; ++i)
    parameters.push_back(0.25F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden1) * 2 * kNnueAccumulatorDimensions, 0.02F);
  for (std::size_t i = 0; i < kHidden1; ++i) parameters.push_back(0.05F + next_value(state, 0.01F));
  append(static_cast<std::size_t>(kHidden2) * kHidden1, 0.04F);
  for (std::size_t i = 0; i < kHidden2; ++i) parameters.push_back(0.20F + next_value(state, 0.01F));
  append(kHidden2, 100.0F);
  parameters.push_back(next_value(state, 10.0F));
  require(parameters.size() == kParameterCount, "deterministic parameter count");

  const auto* payload = reinterpret_cast<const std::uint8_t*>(parameters.data());
  const std::size_t payload_size = parameters.size() * sizeof(float);
  HeaderV3 header{{'H', 'E', 'B', 'I', 'N', 'N', 'U', 'E'}, 3, kNnueFeatureSetV1,
                  static_cast<std::uint32_t>(kNnueInputDimensions),
                  static_cast<std::uint32_t>(kNnueAccumulatorDimensions),
                  kHidden1, kHidden2, 1, 1, 0x01020304U, 2, 1.0F,
                  kParameterCount, fnv1a_bytes(payload, payload_size)};
  std::vector<std::uint8_t> bytes(sizeof(header) + payload_size);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::memcpy(bytes.data() + sizeof(header), payload, payload_size);
  return bytes;
}

Square square_from_coordinate(std::string_view coordinate) {
  require(coordinate.size() == 2 && coordinate[0] >= 'a' && coordinate[0] <= 'h' &&
              coordinate[1] >= '1' && coordinate[1] <= '8',
          "valid square coordinate");
  return Square::from_file_rank(static_cast<std::uint8_t>(coordinate[0] - 'a'),
                                static_cast<std::uint8_t>(coordinate[1] - '1'));
}

PieceType promotion_from_symbol(char symbol) {
  switch (symbol) {
    case 'q': return PieceType::Queen;
    case 'r': return PieceType::Rook;
    case 'b': return PieceType::Bishop;
    case 'n': return PieceType::Knight;
    default: require(false, "valid promotion symbol"); return PieceType::None;
  }
}

Move legal_move_for(const Board& board, std::string_view coordinate) {
  require(coordinate.size() == 4 || coordinate.size() == 5, "valid move coordinate");
  const Square from = square_from_coordinate(coordinate.substr(0, 2));
  const Square to = square_from_coordinate(coordinate.substr(2, 2));
  const PieceType promotion = coordinate.size() == 5
      ? promotion_from_symbol(coordinate[4]) : PieceType::None;
  Board probe = board;
  for (const Move& move : generate_legal_moves(probe)) {
    if (move.from == from && move.to == to && move.promotion == promotion) return move;
  }
  require(false, "requested move must be legal: " + std::string(coordinate));
  return {};
}

float accumulator_difference(const NnueAccumulator& left,
                             const NnueAccumulator& right) {
  float maximum = 0.0F;
  const auto compare = [&](const auto& a, const auto& b) {
    for (std::size_t i = 0; i < a.size(); ++i)
      maximum = std::max(maximum, std::fabs(a[i] - b[i]));
  };
  compare(left.white, right.white);
  compare(left.black, right.black);
  return maximum;
}

std::size_t occupied_square_count(const Board& board) {
  std::size_t count = 0;
  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    if (!board.piece_at(Square::from_index(index)).is_empty()) ++count;
  }
  return count;
}

void check_child(const Board& child, const NnueAccumulator& incremental,
                 Measurements& measurements) {
  NnueAccumulator rebuilt;
  require(refresh_nnue_accumulator(child, rebuilt), "full child accumulator refresh");
  const float accumulator_difference_value = accumulator_difference(incremental, rebuilt);
  measurements.accumulator_max_abs_diff = std::max(measurements.accumulator_max_abs_diff,
                                                    accumulator_difference_value);
  require(accumulator_difference_value <= 1e-4F, "child accumulator difference <= 1e-4");
  const auto incremental_eval =
      evaluate_nnue_network_raw_from_accumulator(child, incremental);
  const auto rebuilt_eval = evaluate_nnue_network_raw_from_accumulator(child, rebuilt);
  require(incremental_eval.has_value() && rebuilt_eval.has_value(), "raw child evaluations");
  const float eval_difference = std::fabs(*incremental_eval - *rebuilt_eval);
  measurements.eval_max_abs_diff_cp = std::max(measurements.eval_max_abs_diff_cp,
                                                eval_difference);
  require(eval_difference <= 0.001F, "raw child evaluation difference <= 0.001 cp");
  const auto legacy_eval = evaluate_nnue_network_raw(child);
  require(legacy_eval.has_value(), "legacy full child evaluation");
  const float composed_difference = std::fabs(*legacy_eval - *incremental_eval);
  measurements.composed_max_abs_diff_cp = std::max(measurements.composed_max_abs_diff_cp,
                                                    composed_difference);
  require(composed_difference <= 0.001F,
          "legacy versus incremental composed child evaluation difference <= 0.001 cp");
}

void verify_targeted_case(std::string_view name, const std::string& fen,
                          std::string_view coordinate, Measurements& measurements) {
  const auto parsed = Board::from_fen(fen);
  require(parsed.has_value(), "valid targeted FEN: " + std::string(name));
  Board parent = *parsed;
  const std::string parent_fen = parent.to_fen();
  NnueAccumulator parent_accumulator;
  require(refresh_nnue_accumulator(parent, parent_accumulator), "parent accumulator refresh");
  const Move move = legal_move_for(parent, coordinate);
  NnueAccumulator incremental;
  require(update_nnue_accumulator(parent, move, parent_accumulator, incremental),
          "incremental targeted update: " + std::string(name));
  const UndoState undo = parent.make_move(move);
  check_child(parent, incremental, measurements);
  parent.unmake_move(move, undo);
  require(parent.to_fen() == parent_fen, "targeted unmake restores board: " + std::string(name));
  NnueAccumulator restored;
  require(refresh_nnue_accumulator(parent, restored), "restored accumulator refresh");
  require(accumulator_difference(parent_accumulator, restored) == 0.0F,
          "targeted unmake restores accumulator: " + std::string(name));
  ++measurements.targeted_cases;
}

struct TargetCase {
  const char* name;
  const char* fen;
  const char* move;
};

const std::vector<TargetCase>& target_cases() {
  static const std::vector<TargetCase> cases = {
      {"quiet pawn", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e3"},
      {"double pawn push", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4"},
      {"knight", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3"},
      {"bishop", "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", "c1g5"},
      {"rook", "4k3/8/8/8/8/8/8/R3K3 w - - 0 1", "a1a5"},
      {"queen", "4k3/8/8/8/8/8/8/3QK3 w - - 0 1", "d1d5"},
      {"normal capture", "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5"},
      {"en passant", "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6"},
      {"white O-O", "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1"},
      {"white O-O-O", "4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1c1"},
      {"black O-O", "r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1", "e8g8"},
      {"black O-O-O", "r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1", "e8c8"},
      {"promotion queen", "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8q"},
      {"promotion rook", "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8r"},
      {"promotion bishop", "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8b"},
      {"promotion knight", "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7a8n"},
      {"promotion capture", "1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", "a7b8q"},
      {"white king", "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "e1e2"},
      {"black king", "4k3/8/8/8/8/8/8/4K3 b - - 0 1", "e8e7"},
  };
  return cases;
}

void run_targeted_cases(Measurements& measurements) {
  for (const TargetCase& target : target_cases())
    verify_targeted_case(target.name, target.fen, target.move, measurements);
}

void run_random_legal_walk(Measurements& measurements) {
  const std::vector<std::string> seed_fens = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r1bqkbnr/pppp1ppp/2n1p3/3p4/3P4/2N1PN2/PPP2PPP/R1BQKB1R w KQkq - 2 4",
      "r3k2r/ppp1bppp/2n1pn2/3p4/3P4/2N1PN2/PPP1BPPP/R3K2R w KQkq - 0 8",
      "4k3/2p2pp1/1p1p3p/3P4/2P1P3/1P4P1/P4P1P/4K3 w - - 0 25",
  };
  std::mt19937 random(0xA66A3D11U);
  std::size_t seed_index = 0;
  auto make_seed = [&]() {
    const auto board = Board::from_fen(seed_fens[seed_index++ % seed_fens.size()]);
    require(board.has_value(), "valid random-walk seed FEN");
    return *board;
  };
  Board board = make_seed();
  NnueAccumulator accumulator;
  require(refresh_nnue_accumulator(board, accumulator), "random-walk initial refresh");
  while (measurements.random_transitions < 4096) {
    std::vector<Move> legal = generate_legal_moves(board);
    if (legal.empty()) {
      board = make_seed();
      require(refresh_nnue_accumulator(board, accumulator), "random-walk restart refresh");
      continue;
    }
    const Move move = legal[random() % legal.size()];
    const Board parent = board;
    const NnueAccumulator parent_accumulator = accumulator;
    NnueAccumulator child_accumulator;
    require(update_nnue_accumulator(parent, move, parent_accumulator, child_accumulator),
            "random incremental update");
    const UndoState undo = board.make_move(move);
    check_child(board, child_accumulator, measurements);
    ++measurements.random_transitions;
    if (random() % 8 == 0) {
      board.unmake_move(move, undo);
      require(board.to_fen() == parent.to_fen(), "random unmake restores board");
      require(accumulator_difference(accumulator, parent_accumulator) == 0.0F,
              "random unmake preserves parent accumulator exactly");
      NnueAccumulator restored;
      require(refresh_nnue_accumulator(board, restored), "random unmake refresh");
      const float restored_difference = accumulator_difference(parent_accumulator, restored);
      measurements.accumulator_max_abs_diff = std::max(measurements.accumulator_max_abs_diff,
                                                        restored_difference);
      require(restored_difference <= 1e-4F, "random unmake accumulator rebuild parity");
    } else {
      accumulator = child_accumulator;
    }
  }
}

struct BenchmarkSample {
  Board parent;
  Board child;
  Move move;
  NnueAccumulator parent_accumulator;
  enum class Kind { Ordinary, Capture, KingRefresh } kind;
};

BenchmarkSample::Kind benchmark_kind(const Board& parent, const Move& move) {
  if (parent.piece_at(move.from).type == PieceType::King)
    return BenchmarkSample::Kind::KingRefresh;
  if (move.flag == MoveFlag::Capture || move.flag == MoveFlag::EnPassant ||
      move.flag == MoveFlag::PromotionCapture)
    return BenchmarkSample::Kind::Capture;
  return BenchmarkSample::Kind::Ordinary;
}

std::vector<BenchmarkSample> make_benchmark_samples() {
  constexpr std::size_t kMixedSamples = 4096;
  constexpr std::size_t kMaximumPliesPerWalk = 32;
  const std::vector<std::string> seed_fens = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r1bqkbnr/pppp1ppp/2n1p3/3p4/3P4/2N1PN2/PPP2PPP/R1BQKB1R w KQkq - 2 4",
      "r3k2r/ppp1bppp/2n1pn2/3p4/3P4/2N1PN2/PPP1BPPP/R3K2R w KQkq - 0 8",
      "4k3/2p2pp1/1p1p3p/3P4/2P1P3/1P4P1/P4P1P/4K3 w - - 0 25",
  };
  std::vector<BenchmarkSample> samples;
  samples.reserve(kMixedSamples);
  std::mt19937 random(0xB16B00B5U);
  std::size_t seed_index = 0;
  const auto next_seed = [&]() {
    const auto parsed = Board::from_fen(seed_fens[seed_index++ % seed_fens.size()]);
    require(parsed.has_value(), "valid benchmark seed FEN");
    return *parsed;
  };
  Board board = next_seed();
  std::size_t plies_in_walk = 0;
  while (samples.size() < kMixedSamples) {
    // The benchmark corpus stays within the normal 32-piece feature bound.
    // If a synthetic random walk reaches a malformed state, discard that walk
    // rather than feeding an out-of-domain board to the production evaluator.
    if (occupied_square_count(board) > kNnueMaxActiveFeatures) {
      board = next_seed();
      plies_in_walk = 0;
      continue;
    }
    const std::vector<Move> legal = generate_legal_moves(board);
    if (legal.empty() || plies_in_walk == kMaximumPliesPerWalk) {
      board = next_seed();
      plies_in_walk = 0;
      continue;
    }
    std::vector<Move> non_promotion_moves;
    non_promotion_moves.reserve(legal.size());
    for (const Move& legal_move : legal) {
      if (!legal_move.is_promotion()) non_promotion_moves.push_back(legal_move);
    }
    if (non_promotion_moves.empty()) {
      board = next_seed();
      plies_in_walk = 0;
      continue;
    }
    const Move move = non_promotion_moves[random() % non_promotion_moves.size()];
    BenchmarkSample sample{board, board, move, {}, benchmark_kind(board, move)};
    require(refresh_nnue_accumulator(sample.parent, sample.parent_accumulator),
            "benchmark parent refresh");
    sample.child.make_move(move);
    if (occupied_square_count(sample.child) > kNnueMaxActiveFeatures) {
      board = next_seed();
      plies_in_walk = 0;
      continue;
    }
    board = sample.child;
    samples.push_back(std::move(sample));
    ++plies_in_walk;
  }
  return samples;
}

std::vector<BenchmarkSample> make_targeted_benchmark_samples() {
  std::vector<BenchmarkSample> samples;
  samples.reserve(target_cases().size());
  for (const TargetCase& target : target_cases()) {
    const auto parsed = Board::from_fen(target.fen);
    require(parsed.has_value(), "valid targeted benchmark FEN");
    const Move move = legal_move_for(*parsed, target.move);
    BenchmarkSample sample{*parsed, *parsed, move, {}, benchmark_kind(*parsed, move)};
    require(refresh_nnue_accumulator(sample.parent, sample.parent_accumulator),
            "targeted benchmark parent refresh");
    sample.child.make_move(move);
    samples.push_back(std::move(sample));
  }
  return samples;
}

volatile float benchmark_sink = 0.0F;

template <typename Operation>
double measure_microseconds(const std::vector<BenchmarkSample>& samples,
                            std::size_t repeats, Operation operation) {
  float sum = 0.0F;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t repeat = 0; repeat < repeats; ++repeat)
    for (const BenchmarkSample& sample : samples) sum += operation(sample);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  benchmark_sink = benchmark_sink + sum;
  return std::chrono::duration<double, std::micro>(elapsed).count() /
      static_cast<double>(repeats * samples.size());
}

struct EvaluationTiming {
  std::size_t samples{0};
  double legacy_full_us{0.0};
  double incremental_total_us{0.0};
};

struct BenchmarkResults {
  EvaluationTiming ordinary;
  EvaluationTiming capture;
  EvaluationTiming king_refresh;
  EvaluationTiming mixed;
  NnueEvaluatorStageProfile stages{};
};

EvaluationTiming measure_total_evaluation(const std::vector<BenchmarkSample>& samples,
                                          std::size_t repeats) {
  require(!samples.empty(), "non-empty total evaluation benchmark class");
  EvaluationTiming timing;
  timing.samples = samples.size();
  timing.legacy_full_us = measure_microseconds(samples, repeats,
      [](const BenchmarkSample& sample) {
        const auto score = evaluate_nnue_network_raw(sample.child);
        require(score.has_value(), "benchmark legacy full evaluation");
        return *score;
      });
  timing.incremental_total_us = measure_microseconds(samples, repeats,
      [](const BenchmarkSample& sample) {
        // update_nnue_accumulator begins by copying the parent accumulator;
        // the timed interval intentionally includes that copy and the forward.
        NnueAccumulator accumulator;
        require(update_nnue_accumulator(sample.parent, sample.move,
                                        sample.parent_accumulator, accumulator),
                "benchmark incremental child update");
        const auto score = evaluate_nnue_network_raw_from_accumulator(sample.child, accumulator);
        require(score.has_value(), "benchmark incremental total evaluation");
        return *score;
      });
  return timing;
}

std::vector<BenchmarkSample> select_samples(
    const std::vector<BenchmarkSample>& all, BenchmarkSample::Kind kind) {
  std::vector<BenchmarkSample> selected;
  std::copy_if(all.begin(), all.end(), std::back_inserter(selected),
               [kind](const BenchmarkSample& sample) { return sample.kind == kind; });
  return selected;
}

BenchmarkResults run_benchmarks(Measurements& measurements) {
  const std::vector<BenchmarkSample> mixed = make_benchmark_samples();
  const std::vector<BenchmarkSample> targeted = make_targeted_benchmark_samples();
  const std::vector<BenchmarkSample> ordinary =
      select_samples(targeted, BenchmarkSample::Kind::Ordinary);
  const std::vector<BenchmarkSample> captures =
      select_samples(targeted, BenchmarkSample::Kind::Capture);
  const std::vector<BenchmarkSample> king_refresh =
      select_samples(targeted, BenchmarkSample::Kind::KingRefresh);
  require(!ordinary.empty() && !captures.empty() && !king_refresh.empty(),
          "all benchmark move classes must be represented");

  for (const BenchmarkSample& sample : mixed) {
    NnueAccumulator accumulator;
    require(update_nnue_accumulator(sample.parent, sample.move,
                                    sample.parent_accumulator, accumulator),
            "composed benchmark incremental update");
    const auto legacy = evaluate_nnue_network_raw(sample.child);
    const auto incremental =
        evaluate_nnue_network_raw_from_accumulator(sample.child, accumulator);
    require(legacy.has_value() && incremental.has_value(), "composed benchmark evaluations");
    const float difference = std::fabs(*legacy - *incremental);
    measurements.composed_max_abs_diff_cp =
        std::max(measurements.composed_max_abs_diff_cp, difference);
    require(difference <= 0.001F, "composed benchmark evaluation difference <= 0.001 cp");
  }

  std::vector<Board> mixed_children;
  mixed_children.reserve(mixed.size());
  for (const BenchmarkSample& sample : mixed) mixed_children.push_back(sample.child);
  const auto stages = profile_nnue_evaluator_stages(mixed_children, 64);
  require(stages.has_value(), "evaluator stage profile");

  BenchmarkResults results;
  results.ordinary = measure_total_evaluation(ordinary, 64);
  results.capture = measure_total_evaluation(captures, 64);
  results.king_refresh = measure_total_evaluation(king_refresh, 64);
  results.mixed = measure_total_evaluation(mixed, 32);
  results.stages = *stages;
  return results;
}

double speedup_percent(const EvaluationTiming& timing) {
  return 100.0 * (timing.legacy_full_us - timing.incremental_total_us) /
      timing.legacy_full_us;
}

double saved_microseconds(const EvaluationTiming& timing) {
  return timing.legacy_full_us - timing.incremental_total_us;
}

void print_evaluation_timing(const char* name, const EvaluationTiming& timing) {
  std::cout << "NNUE composed " << name
            << ": samples=" << timing.samples
            << " legacy_full_us=" << timing.legacy_full_us
            << " incremental_update_plus_forward_us=" << timing.incremental_total_us
            << " speedup_percent=" << speedup_percent(timing)
            << " saved_us_per_eval=" << saved_microseconds(timing) << '\n';
}

void print_stage(const char* name, double value, double total) {
  std::cout << "NNUE evaluator stage " << name
            << ": us=" << value
            << " percent_of_profile=" << 100.0 * value / total << '\n';
}

}  // namespace

int main() {
  const std::vector<std::uint8_t> network = deterministic_network();
  std::string error;
  require(load_nnue_network_bytes(network.data(), network.size(), error), error);
  Measurements measurements;
  run_targeted_cases(measurements);
  run_random_legal_walk(measurements);
  const BenchmarkResults benchmarks = run_benchmarks(measurements);
  clear_nnue_network();

  std::cout << std::fixed << std::setprecision(6)
            << "NNUE incremental accumulator: targeted_cases=" << measurements.targeted_cases
            << " random_transitions=" << measurements.random_transitions
            << " accumulator_max_abs_diff=" << measurements.accumulator_max_abs_diff
            << " eval_max_abs_diff_cp=" << measurements.eval_max_abs_diff_cp
            << " composed_max_abs_diff_cp=" << measurements.composed_max_abs_diff_cp << '\n';
  print_evaluation_timing("ordinary", benchmarks.ordinary);
  print_evaluation_timing("capture", benchmarks.capture);
  print_evaluation_timing("king_refresh", benchmarks.king_refresh);
  print_evaluation_timing("mixed_realistic", benchmarks.mixed);
  const double stage_total = benchmarks.stages.accumulator_rebuild_us +
      benchmarks.stages.clip_precompute_us + benchmarks.stages.hidden1_dense_us +
      benchmarks.stages.hidden1_activation_hidden2_us + benchmarks.stages.output_us;
  print_stage("feature_enumeration_plus_accumulator_rebuild",
              benchmarks.stages.accumulator_rebuild_us, stage_total);
  print_stage("accumulator_clipping_precompute", benchmarks.stages.clip_precompute_us,
              stage_total);
  print_stage("hidden1_dense", benchmarks.stages.hidden1_dense_us, stage_total);
  print_stage("hidden1_activation_plus_hidden2",
              benchmarks.stages.hidden1_activation_hidden2_us, stage_total);
  print_stage("output_layer", benchmarks.stages.output_us, stage_total);
  return 0;
}
