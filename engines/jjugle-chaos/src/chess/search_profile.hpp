#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <chrono>

#ifndef HEBICHESS_SEARCH_PROFILE
#define HEBICHESS_SEARCH_PROFILE 0
#endif
#ifndef HEBICHESS_SEARCH_PROFILE_TIMING
#define HEBICHESS_SEARCH_PROFILE_TIMING HEBICHESS_SEARCH_PROFILE
#endif

namespace hebichess {

enum class ProfileMetric : std::size_t {
  MainSearch,
  QSearch,
  Evaluation,
  MoveGeneration,
  LegalFiltering,
  BoardMake,
  BoardUnmake,
  TranspositionTable,
  See,
  StalemateLegality,
  NnueWrapper,
  AccumulatorUpdate,
  AccumulatorRefresh,
  PerspectiveRefresh,
  FeatureEnumeration,
  AccumulatorCopy,
  InputClipping,
  Hidden1Dense,
  Hidden1Activation,
  Hidden2DenseActivation,
  OutputLayer,
  OutputConversion,
  ForwardBufferAllocation,
  Count,
};

enum class ProfileCounter : std::size_t {
  EvaluateApiCalls,
  EvaluationRequests,
  MainEvaluationRequests,
  QsearchEvaluationRequests,
  NnueEvaluationCalls,
  MainNnueEvaluationCalls,
  QsearchNnueEvaluationCalls,
  HceEvaluationCalls,
  AccumulatorIncrementalUpdates,
  FullAccumulatorRebuilds,
  PerspectiveAccumulatorRebuilds,
  AccumulatorCopies,
  AccumulatorCopyBytes,
  FeatureExtractions,
  ActiveFeatures,
  Hidden1Invocations,
  Hidden2Invocations,
  OutputLayerInvocations,
  Count,
};

#if HEBICHESS_SEARCH_PROFILE

struct SearchProfileStats {
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileMetric::Count)> calls{};
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileMetric::Count)> samples{};
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileMetric::Count)> estimated_ns{};
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileCounter::Count)> counters{};
};

inline thread_local SearchProfileStats* active_search_profile = nullptr;

inline void profile_add(ProfileCounter counter, std::uint64_t amount = 1) noexcept {
  if (active_search_profile != nullptr)
    active_search_profile->counters[static_cast<std::size_t>(counter)] += amount;
}

class SearchProfileBinding {
 public:
  explicit SearchProfileBinding(SearchProfileStats& profile) noexcept
      : previous_(active_search_profile) { active_search_profile = &profile; }
  ~SearchProfileBinding() { active_search_profile = previous_; }
  SearchProfileBinding(const SearchProfileBinding&) = delete;
  SearchProfileBinding& operator=(const SearchProfileBinding&) = delete;
 private:
  SearchProfileStats* previous_;
};

#if HEBICHESS_SEARCH_PROFILE_TIMING
class SampledProfileTimer {
 public:
  explicit SampledProfileTimer(ProfileMetric metric, std::uint64_t sample_period = 128) noexcept
      : profile_(active_search_profile), metric_(static_cast<std::size_t>(metric)),
        period_(sample_period == 0 ? 1 : sample_period) {
    if (profile_ == nullptr) return;
    const std::uint64_t call = ++profile_->calls[metric_];
    if (call % period_ == 0) {
      sampled_ = true;
      ++profile_->samples[metric_];
      started_ = std::chrono::steady_clock::now();
    }
  }
  ~SampledProfileTimer() { stop(); }
  void stop() noexcept {
    if (!sampled_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_).count();
    if (elapsed > 0)
      profile_->estimated_ns[metric_] += static_cast<std::uint64_t>(elapsed) * period_;
    sampled_ = false;
  }
  SampledProfileTimer(const SampledProfileTimer&) = delete;
  SampledProfileTimer& operator=(const SampledProfileTimer&) = delete;
 private:
  SearchProfileStats* profile_{};
  std::size_t metric_{};
  std::uint64_t period_{128};
  bool sampled_{false};
  std::chrono::steady_clock::time_point started_{};
};
#else
class SampledProfileTimer {
 public:
  explicit SampledProfileTimer(ProfileMetric, std::uint64_t = 128) noexcept {}
  void stop() noexcept {}
};
#endif

#else

struct SearchProfileStats {};
inline void profile_add(ProfileCounter, std::uint64_t = 1) noexcept {}
class SearchProfileBinding {
 public:
  explicit SearchProfileBinding(SearchProfileStats&) noexcept {}
};
class SampledProfileTimer {
 public:
  explicit SampledProfileTimer(ProfileMetric, std::uint64_t = 128) noexcept {}
  void stop() noexcept {}
};

#endif

}  // namespace hebichess
