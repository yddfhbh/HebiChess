#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <chrono>

#ifndef HEBICHESS_SEARCH_PROFILE
#define HEBICHESS_SEARCH_PROFILE 0
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
  Count,
};

#if HEBICHESS_SEARCH_PROFILE

struct SearchProfileStats {
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileMetric::Count)> calls{};
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileMetric::Count)> samples{};
  std::array<std::uint64_t, static_cast<std::size_t>(ProfileMetric::Count)> estimated_ns{};
};

inline thread_local SearchProfileStats* active_search_profile = nullptr;

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
  ~SampledProfileTimer() {
    if (!sampled_) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started_).count();
    if (elapsed > 0)
      profile_->estimated_ns[metric_] += static_cast<std::uint64_t>(elapsed) * period_;
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

struct SearchProfileStats {};
class SearchProfileBinding {
 public:
  explicit SearchProfileBinding(SearchProfileStats&) noexcept {}
};
class SampledProfileTimer {
 public:
  explicit SampledProfileTimer(ProfileMetric, std::uint64_t = 128) noexcept {}
};

#endif

}  // namespace hebichess
