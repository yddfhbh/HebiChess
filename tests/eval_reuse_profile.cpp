#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "chess/nnue.hpp"
#include "chess/search.hpp"

using namespace hebichess;

namespace {

struct Fixture {
  std::string name;
  std::string fen;
  int depth;
};

const Fixture kFixtures[]{
    {"reproduced-endgame", "8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1", 10},
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5},
    {"quiet-middlegame", "rn1qk3/2p1p1b1/pp3npr/3p1p1p/P2P2P1/RPN4b/1BP1PP1P/1Q2KBNR w Kq - 1 12", 5},
    {"tactical-middlegame", "rn1q1k2/4p2r/ppp2npb/5p2/P2Pp3/BPN5/R1P1KPbP/1Q3BNR w - - 2 18", 5},
    {"king-attack", "2r1n1k1/3np2r/1ppq4/p1BNb1pQ/P1BPp3/1P6/2P2PbP/1R2K1NR w - - 14 30", 5},
};

struct Fingerprint {
  std::uint64_t a;
  std::uint64_t b;
  bool operator==(const Fingerprint&) const = default;
};

struct KeyState {
  ZobristKey key;
  Color side_to_move;
  Fingerprint fingerprint;
  bool operator==(const KeyState&) const = default;
};

struct KeyStateHash {
  std::size_t operator()(const KeyState& value) const noexcept {
    std::uint64_t hash = value.key ^ (static_cast<std::uint64_t>(value.side_to_move) << 53) ^
                         std::rotl(value.fingerprint.a, 17) ^
                         std::rotl(value.fingerprint.b, 39);
    hash ^= hash >> 30;
    hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27;
    hash *= 0x94d049bb133111ebULL;
    return static_cast<std::size_t>(hash ^ (hash >> 31));
  }
};

std::uint64_t index_hash(std::uint64_t value) {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

struct CacheStats {
  std::uint64_t hits{0};
  std::uint64_t safe_hits{0};
  std::uint64_t unsafe_hits{0};
  std::uint64_t collisions{0};
  std::uint64_t replacements{0};
};

struct CacheEntry {
  ZobristKey key{0};
  Color side_to_move{Color::White};
  Fingerprint fingerprint{};
  bool occupied{false};
  int root_iteration{0};
  int aspiration_retry{0};
};

struct AssociativeCacheStats {
  std::uint64_t probes{0};
  std::uint64_t hits{0};
  std::uint64_t misses{0};
  std::uint64_t compulsory_misses{0};
  std::uint64_t revisit_misses{0};
  std::uint64_t inserts{0};
  std::uint64_t replacements{0};
  std::uint64_t board_key_comparisons{0};
  std::uint64_t board_key_matches{0};
  std::uint64_t unrelated_board_candidates{0};
  std::uint64_t unrelated_board_replacements{0};
  std::uint64_t accumulator_mismatches{0};
  std::uint64_t ways_examined{0};
  std::uint64_t accumulator_compares{0};
  std::uint64_t bytes_compared{0};
  std::uint64_t evicted_states_requested_again{0};
  std::uint64_t evicted_lost_reuse_requests{0};
  std::uint64_t same_attempt_hits{0};
  std::uint64_t earlier_retry_hits{0};
  std::uint64_t earlier_iteration_hits{0};
};

AssociativeCacheStats simulate_associative(const std::vector<EvalReuseRequest>& events,
                                            std::size_t total_entries,
                                            std::size_t ways,
                                            int lifetime = 0) {
  const std::size_t set_count = total_entries / ways;
  std::vector<std::vector<CacheEntry>> sets(set_count,
      std::vector<CacheEntry>(ways));
  std::vector<std::size_t> next_victim(set_count, 0);
  std::unordered_set<KeyState, KeyStateHash> evicted_pending;
  std::unordered_set<KeyState, KeyStateHash> evicted_seen_again;
  std::unordered_set<KeyState, KeyStateHash> seen_states;
  AssociativeCacheStats stats;
  int last_iteration = -1, last_retry = -1;
  for (const EvalReuseRequest& event : events) {
    const bool iteration_changed = event.root_iteration != last_iteration;
    const bool retry_changed = event.aspiration_retry != last_retry;
    const bool clear = (lifetime >= 1 && iteration_changed) ||
        (lifetime >= 2 && (iteration_changed || retry_changed));
    if (clear) {
      for (auto& set : sets) std::fill(set.begin(), set.end(), CacheEntry{});
      std::fill(next_victim.begin(), next_victim.end(), 0);
    }
    last_iteration = event.root_iteration;
    last_retry = event.aspiration_retry;
    const Fingerprint fp{event.accumulator_hash_a, event.accumulator_hash_b};
    const KeyState state{event.key, event.side_to_move, fp};
    const std::size_t set_index = static_cast<std::size_t>(event.key) & (set_count - 1);
    auto& set = sets[set_index];
    ++stats.probes;
    CacheEntry* hit_entry = nullptr;
    for (std::size_t way = 0; way < ways; ++way) {
      ++stats.ways_examined;
      CacheEntry& entry = set[way];
      if (!entry.occupied) continue;
      ++stats.board_key_comparisons;
      if (entry.key != event.key || entry.side_to_move != event.side_to_move) {
        ++stats.unrelated_board_candidates;
        continue;
      }
      ++stats.board_key_matches;
      ++stats.accumulator_compares;
      stats.bytes_compared += sizeof(NnueAccumulator);
      if (entry.fingerprint == fp) { hit_entry = &entry; break; }
      ++stats.accumulator_mismatches;
    }
    if (hit_entry != nullptr) {
      ++stats.hits;
      if (hit_entry->root_iteration < event.root_iteration)
        ++stats.earlier_iteration_hits;
      else if (hit_entry->aspiration_retry < event.aspiration_retry)
        ++stats.earlier_retry_hits;
      else
        ++stats.same_attempt_hits;
      continue;
    }
    ++stats.misses;
    if (seen_states.insert(state).second) ++stats.compulsory_misses;
    else ++stats.revisit_misses;
    if (evicted_pending.contains(state) && evicted_seen_again.insert(state).second) {
      ++stats.evicted_states_requested_again;
      ++stats.evicted_lost_reuse_requests;
    }
    std::size_t victim = ways;
    for (std::size_t way = 0; way < ways; ++way) {
      if (!set[way].occupied) { victim = way; break; }
    }
    if (victim == ways) {
      victim = next_victim[set_index];
      next_victim[set_index] = (next_victim[set_index] + 1) % ways;
      ++stats.replacements;
      const CacheEntry& old = set[victim];
      if (old.key != event.key || old.side_to_move != event.side_to_move)
        ++stats.unrelated_board_replacements;
      evicted_pending.insert(KeyState{old.key, old.side_to_move, old.fingerprint});
    } else {
      ++stats.inserts;
    }
    set[victim] = CacheEntry{event.key, event.side_to_move, fp, true,
                             event.root_iteration, event.aspiration_retry};
  }
  return stats;
}

AssociativeCacheStats simulate_fully_associative_optimal(
    const std::vector<EvalReuseRequest>& events, std::size_t capacity) {
  const std::size_t never = events.size();
  std::vector<std::size_t> next_use(events.size(), never);
  std::unordered_map<KeyState, std::size_t, KeyStateHash> next_by_state;
  for (std::size_t i = events.size(); i-- > 0;) {
    const EvalReuseRequest& event = events[i];
    const KeyState state{event.key, event.side_to_move,
        Fingerprint{event.accumulator_hash_a, event.accumulator_hash_b}};
    if (const auto it = next_by_state.find(state); it != next_by_state.end())
      next_use[i] = it->second;
    next_by_state[state] = i;
  }
  std::unordered_map<KeyState, std::size_t, KeyStateHash> resident_next;
  std::priority_queue<std::pair<std::size_t, std::size_t>> farthest_next;
  std::unordered_set<KeyState, KeyStateHash> seen_states;
  std::unordered_set<KeyState, KeyStateHash> evicted_pending;
  std::unordered_set<KeyState, KeyStateHash> evicted_seen_again;
  AssociativeCacheStats stats;
  for (std::size_t i = 0; i < events.size(); ++i) {
    const EvalReuseRequest& event = events[i];
    const KeyState state{event.key, event.side_to_move,
        Fingerprint{event.accumulator_hash_a, event.accumulator_hash_b}};
    ++stats.probes;
    if (resident_next.contains(state)) {
      ++stats.hits;
      resident_next[state] = next_use[i];
      farthest_next.emplace(next_use[i], i);
      continue;
    }
    ++stats.misses;
    if (seen_states.insert(state).second) ++stats.compulsory_misses;
    else ++stats.revisit_misses;
    if (evicted_pending.contains(state) && evicted_seen_again.insert(state).second) {
      ++stats.evicted_states_requested_again;
      ++stats.evicted_lost_reuse_requests;
    }
    if (resident_next.size() == capacity) {
      KeyState victim{};
      std::size_t victim_next = 0;
      for (;;) {
        const auto [candidate_next, candidate_index] = farthest_next.top();
        farthest_next.pop();
        const EvalReuseRequest& candidate_event = events[candidate_index];
        const KeyState candidate{candidate_event.key, candidate_event.side_to_move,
            Fingerprint{candidate_event.accumulator_hash_a, candidate_event.accumulator_hash_b}};
        const auto it = resident_next.find(candidate);
        if (it != resident_next.end() && it->second == candidate_next) {
          victim = candidate;
          victim_next = candidate_next;
          break;
        }
      }
      (void)victim_next;
      resident_next.erase(victim);
      evicted_pending.insert(victim);
      ++stats.replacements;
    }
    resident_next[state] = next_use[i];
    farthest_next.emplace(next_use[i], i);
    ++stats.inserts;
  }
  return stats;
}

CacheStats simulate(const std::vector<EvalReuseRequest>& events, std::size_t capacity,
                    bool zobrist_only, int lifetime) {
  std::unordered_map<KeyState, std::size_t, KeyStateHash> associative_exact;
  std::unordered_map<ZobristKey, Fingerprint> associative_board;
  std::vector<CacheEntry> direct(capacity);
  CacheStats stats;
  int last_iteration = -1, last_retry = -1;
  for (const EvalReuseRequest& event : events) {
    const Fingerprint fp{event.accumulator_hash_a, event.accumulator_hash_b};
    if (lifetime >= 1 && event.root_iteration != last_iteration) {
      std::fill(direct.begin(), direct.end(), CacheEntry{});
      associative_exact.clear(); associative_board.clear();
    } else if (lifetime >= 2 &&
               (event.root_iteration != last_iteration || event.aspiration_retry != last_retry)) {
      std::fill(direct.begin(), direct.end(), CacheEntry{});
      associative_exact.clear(); associative_board.clear();
    }
    last_iteration = event.root_iteration;
    last_retry = event.aspiration_retry;

    bool hit = false, safe = false;
    std::size_t slot = 0;
    if (capacity == 0) {
      if (zobrist_only) {
        const auto it = associative_board.find(event.key);
        if (it != associative_board.end()) {
          hit = true; safe = it->second == fp;
        }
        if (!hit || !safe) associative_board[event.key] = fp;
      } else {
        const KeyState state{event.key, event.side_to_move, fp};
        hit = associative_exact.contains(state); safe = hit;
        if (!hit) associative_exact[state] = 1;
      }
    } else {
      const std::uint64_t selector = zobrist_only ? event.key :
          event.key ^ std::rotl(fp.a, 7) ^ std::rotl(fp.b, 29);
      slot = static_cast<std::size_t>(index_hash(selector) % capacity);
      CacheEntry& entry = direct[slot];
      hit = entry.occupied && entry.key == event.key &&
            (zobrist_only || entry.fingerprint == fp);
      safe = hit && entry.fingerprint == fp;
      if (!hit) {
        if (entry.occupied) {
          if (entry.key != event.key) ++stats.collisions;
          ++stats.replacements;
        }
        entry = {event.key, event.side_to_move, fp, true};
      } else if (zobrist_only && !safe) {
        // A board-only cache would return this prior score and retain entry.
      }
    }
    if (hit) ++stats.hits;
    if (safe) ++stats.safe_hits;
    else if (hit) ++stats.unsafe_hits;
  }
  return stats;
}

const char* tt_state(EvalReuseTtState state) {
  switch (state) {
    case EvalReuseTtState::NotProbed: return "not-probed";
    case EvalReuseTtState::Miss: return "miss";
    case EvalReuseTtState::HitNoCutoff: return "hit-no-cutoff";
  }
  return "unknown";
}

void analyze(const Fixture& fixture, const SearchResult& result, double elapsed_ms) {
  const auto& events = result.eval_reuse_requests;
  std::unordered_map<ZobristKey, std::vector<const EvalReuseRequest*>> by_board;
  for (const auto& event : events) by_board[event.key].push_back(&event);
  std::uint64_t repeated_board = 0, repeated_exact = 0, different_fp = 0;
  std::uint64_t max_repetition = 0;
  std::uint64_t board_once = 0, board_twice = 0, board_3_4 = 0, board_5_8 = 0, board_gt8 = 0;
  std::uint64_t same_fp_raw_same = 0, same_fp_raw_diff = 0;
  std::uint64_t diff_fp_raw_same = 0, diff_fp_raw_diff = 0;
  std::uint64_t same_fp_cp_same = 0, same_fp_cp_diff = 0;
  std::uint64_t diff_fp_cp_same = 0, diff_fp_cp_diff = 0;
  double max_raw_diff = 0.0;
  int max_cp_diff = 0;
  std::uint64_t within_iteration = 0, across_iterations = 0, across_retries = 0;
  std::uint64_t pvs_repeat = 0, different_path_repeat = 0;
  std::uint64_t repeated_after_qtt_miss = 0, repeated_after_qtt_hit = 0;
  std::uint64_t repeated_after_qtt_window_reusable = 0;
  std::uint64_t repeated_after_qtt_prior_iteration = 0;
  std::uint64_t repeated_after_qtt_prior_retry = 0;
  std::uint64_t qtt_no_probe = 0;
  std::uint64_t exact_after_qtt_miss = 0, exact_after_qtt_hit = 0;
  std::unordered_set<KeyState, KeyStateHash> seen_exact_states;
  for (const auto& event : events) {
    const KeyState state{event.key, event.side_to_move,
        Fingerprint{event.accumulator_hash_a, event.accumulator_hash_b}};
    const bool exact_repeat = !seen_exact_states.insert(state).second;
    if (exact_repeat && event.qsearch) {
      if (event.qtt_state == EvalReuseTtState::Miss) ++exact_after_qtt_miss;
      else if (event.qtt_state == EvalReuseTtState::HitNoCutoff) ++exact_after_qtt_hit;
    }
  }
  for (const auto& [key, visits] : by_board) {
    (void)key;
    max_repetition = std::max<std::uint64_t>(max_repetition, visits.size());
    if (visits.size() == 1) ++board_once;
    else if (visits.size() == 2) ++board_twice;
    else if (visits.size() <= 4) ++board_3_4;
    else if (visits.size() <= 8) ++board_5_8;
    else ++board_gt8;
    repeated_board += visits.size() > 1 ? visits.size() - 1 : 0;
    std::unordered_map<Fingerprint, std::vector<const EvalReuseRequest*>,
        std::function<std::size_t(const Fingerprint&)>> by_accumulator(
          0, [](const Fingerprint& fp) {
            return static_cast<std::size_t>(fp.a ^ std::rotl(fp.b, 13));
          });
    for (const auto* visit : visits)
      by_accumulator[{visit->accumulator_hash_a, visit->accumulator_hash_b}].push_back(visit);
    for (const auto& [fp, same_state] : by_accumulator) {
      (void)fp;
      if (same_state.size() > 1) repeated_exact += same_state.size() - 1;
      for (std::size_t i = 1; i < same_state.size(); ++i) {
        const auto& first = *same_state.front(); const auto& current = *same_state[i];
        const bool raw_equal = std::bit_cast<std::uint32_t>(first.raw_score) ==
                               std::bit_cast<std::uint32_t>(current.raw_score);
        const bool cp_equal = first.rounded_cp == current.rounded_cp;
        same_fp_raw_same += raw_equal; same_fp_raw_diff += !raw_equal;
        same_fp_cp_same += cp_equal; same_fp_cp_diff += !cp_equal;
        max_raw_diff = std::max(max_raw_diff,
            std::fabs(static_cast<double>(first.raw_score) - current.raw_score));
        max_cp_diff = std::max(max_cp_diff, std::abs(first.rounded_cp - current.rounded_cp));
      }
    }
    const Fingerprint first_fp{visits.front()->accumulator_hash_a,
                               visits.front()->accumulator_hash_b};
    for (std::size_t i = 1; i < visits.size(); ++i) {
      const auto& prior = *visits[i - 1]; const auto& current = *visits[i];
      const Fingerprint current_fp{current.accumulator_hash_a, current.accumulator_hash_b};
      if (!(current_fp == first_fp)) ++different_fp;
      if (prior.root_iteration == current.root_iteration) {
        ++within_iteration;
        if (prior.aspiration_retry != current.aspiration_retry) ++across_retries;
      } else ++across_iterations;
      if (current.pvs_research) ++pvs_repeat;
      if (prior.path_signature != current.path_signature) ++different_path_repeat;
      if (current.qsearch) {
        if (current.qtt_state == EvalReuseTtState::Miss) ++repeated_after_qtt_miss;
        else if (current.qtt_state == EvalReuseTtState::HitNoCutoff) {
          ++repeated_after_qtt_hit;
          if (current.qtt_window_reusable) ++repeated_after_qtt_window_reusable;
        } else ++qtt_no_probe;
        if (current.qtt_store_iteration >= 0 &&
            current.qtt_store_iteration < current.root_iteration)
          ++repeated_after_qtt_prior_iteration;
        else if (current.qtt_store_iteration == current.root_iteration &&
                 current.aspiration_retry > 0)
          ++repeated_after_qtt_prior_retry;
      }
    }
    if (visits.size() > 1) {
      const auto& first = *visits.front();
      for (std::size_t i = 1; i < visits.size(); ++i) {
        const auto& current = *visits[i];
        if (Fingerprint{current.accumulator_hash_a, current.accumulator_hash_b} == first_fp) continue;
        const bool raw_equal = std::bit_cast<std::uint32_t>(first.raw_score) ==
                               std::bit_cast<std::uint32_t>(current.raw_score);
        const bool cp_equal = first.rounded_cp == current.rounded_cp;
        diff_fp_raw_same += raw_equal; diff_fp_raw_diff += !raw_equal;
        diff_fp_cp_same += cp_equal; diff_fp_cp_diff += !cp_equal;
        max_raw_diff = std::max(max_raw_diff,
            std::fabs(static_cast<double>(first.raw_score) - current.raw_score));
        max_cp_diff = std::max(max_cp_diff, std::abs(first.rounded_cp - current.rounded_cp));
      }
    }
  }
  const double total = static_cast<double>(events.size());
  std::cout << "EVAL_REUSE fixture=" << fixture.name << " depth=" << fixture.depth
            << " nodes=" << result.nodes << " qnodes=" << result.qnodes
            << " nnue_evaluations=" << events.size() << " elapsed_ms=" << elapsed_ms
            << " retries=" << result.aspiration_retries
            << " repeated_board_visits=" << repeated_board
            << " repeated_board_pct=" << (total ? 100.0 * repeated_board / total : 0.0)
            << " exact_accumulator_repeats=" << repeated_exact
            << " exact_repeat_pct=" << (total ? 100.0 * repeated_exact / total : 0.0)
            << " same_key_different_fp_visits=" << different_fp
            << " max_repetitions=" << max_repetition
            << " board_key_distribution_once/twice/3-4/5-8/>8="
            << board_once << '/' << board_twice << '/' << board_3_4 << '/'
            << board_5_8 << '/' << board_gt8
            << " same_fp_raw_same/diff=" << same_fp_raw_same << '/' << same_fp_raw_diff
            << " same_fp_cp_same/diff=" << same_fp_cp_same << '/' << same_fp_cp_diff
            << " diff_fp_raw_same/diff=" << diff_fp_raw_same << '/' << diff_fp_raw_diff
            << " diff_fp_cp_same/diff=" << diff_fp_cp_same << '/' << diff_fp_cp_diff
            << " max_raw_diff=" << max_raw_diff << " max_cp_diff=" << max_cp_diff
            << " repeats_within_iteration=" << within_iteration
            << " repeats_across_iterations=" << across_iterations
            << " repeats_across_aspiration_retries=" << across_retries
            << " repeated_pvs_research=" << pvs_repeat
            << " repeated_different_path=" << different_path_repeat
            << " repeated_board_after_qtt_miss=" << repeated_after_qtt_miss
            << " repeated_board_after_qtt_hit_no_cutoff=" << repeated_after_qtt_hit
            << " repeated_board_qtt_window_reusable=" << repeated_after_qtt_window_reusable
            << " repeated_board_qtt_entry_prior_iteration=" << repeated_after_qtt_prior_iteration
            << " repeated_board_qtt_entry_prior_retry=" << repeated_after_qtt_prior_retry
            << " qsearch_repeats_no_qtt_probe=" << qtt_no_probe << '\n';

  const CacheStats all_z = simulate(events, 0, true, 0);
  const CacheStats all_exact = simulate(events, 0, false, 0);
  std::cout << "EVAL_CACHE_SIM fixture=" << fixture.name << " policy=unbounded-zobrist"
            << " hits=" << all_z.hits << " safe=" << all_z.safe_hits
            << " unsafe=" << all_z.unsafe_hits << '\n';
  std::cout << "EVAL_CACHE_SIM fixture=" << fixture.name << " policy=unbounded-key+accumulator-fingerprint"
            << " hits=" << all_exact.hits << " exact_safe=" << all_exact.safe_hits << '\n';
  for (std::size_t size : {1024U, 4096U, 16384U, 65536U}) {
    const CacheStats z = simulate(events, size, true, 0);
    const CacheStats exact = simulate(events, size, false, 0);
    std::cout << "EVAL_CACHE_SIM fixture=" << fixture.name << " entries=" << size
              << " zobrist_hits/safe/unsafe/collisions/replacements=" << z.hits << '/'
              << z.safe_hits << '/' << z.unsafe_hits << '/' << z.collisions << '/'
              << z.replacements << " exact_hits/collisions/replacements=" << exact.safe_hits
              << '/' << exact.collisions << '/' << exact.replacements << '\n';
  }
  const AssociativeCacheStats full_opt = simulate_fully_associative_optimal(events, 4096);
  std::cout << "EVAL_CACHE_ASSOC_CAPACITY fixture=" << fixture.name
            << " entries=4096 policy=fully-associative-OPT-lower-bound"
            << " exact_safe_hits=" << full_opt.hits
            << " compulsory_misses=" << full_opt.compulsory_misses
            << " capacity_revisit_misses=" << full_opt.revisit_misses
            << " replacements=" << full_opt.replacements
            << " evicted_states_requested_again=" << full_opt.evicted_states_requested_again
            << '\n';
  for (std::size_t ways : {1U, 2U, 4U}) {
    const AssociativeCacheStats assoc = simulate_associative(events, 4096, ways);
    const std::uint64_t conflict_excess = assoc.revisit_misses > full_opt.revisit_misses
        ? assoc.revisit_misses - full_opt.revisit_misses : 0;
    std::cout << "EVAL_CACHE_ASSOC fixture=" << fixture.name
              << " entries=4096 ways=" << ways
              << " sets=" << (4096 / ways)
              << " exact_safe_hits=" << assoc.hits
              << " hit_pct=" << (total ? 100.0 * assoc.hits / total : 0.0)
              << " compulsory_misses=" << assoc.compulsory_misses
              << " revisit_misses=" << assoc.revisit_misses
              << " conflict_excess_vs_full_OPT=" << conflict_excess
              << " inserts=" << assoc.inserts
              << " replacements=" << assoc.replacements
              << " unrelated_board_candidates=" << assoc.unrelated_board_candidates
              << " unrelated_board_replacements=" << assoc.unrelated_board_replacements
              << " evicted_states_requested_again=" << assoc.evicted_states_requested_again
              << " evicted_lost_reuse_requests=" << assoc.evicted_lost_reuse_requests
              << " avg_ways_examined=" << (assoc.probes ?
                  static_cast<double>(assoc.ways_examined) / assoc.probes : 0.0)
              << " board_key_matches=" << assoc.board_key_matches
              << " board_key_comparisons=" << assoc.board_key_comparisons
              << " accumulator_mismatches=" << assoc.accumulator_mismatches
              << " memcmp_calls=" << assoc.accumulator_compares
              << " memcmp_bytes=" << assoc.bytes_compared << '\n';
  }
  for (std::size_t entries : {1024U, 2048U, 4096U, 8192U}) {
    const AssociativeCacheStats assoc = simulate_associative(events, entries, 2);
    std::cout << "EVAL_CACHE_BUDGET fixture=" << fixture.name
              << " entries=" << entries << " ways=2"
              << " exact_safe_hits=" << assoc.hits
              << " hit_pct=" << (total ? 100.0 * assoc.hits / total : 0.0)
              << " replacements=" << assoc.replacements
              << " evicted_states_requested_again=" << assoc.evicted_states_requested_again
              << " avg_ways_examined=" << (assoc.probes ?
                  static_cast<double>(assoc.ways_examined) / assoc.probes : 0.0)
              << '\n';
  }
  {
    const AssociativeCacheStats assoc = simulate_associative(events, 8192, 2);
    std::cout << "EVAL_CACHE_ORIGIN fixture=" << fixture.name
              << " entries=8192 ways=2 exact_fingerprint_trace=1"
              << " total_hits=" << assoc.hits
              << " same_attempt=" << assoc.same_attempt_hits
              << " earlier_aspiration_retry=" << assoc.earlier_retry_hits
              << " earlier_iteration=" << assoc.earlier_iteration_hits << '\n';
  }
  for (int lifetime = 1; lifetime <= 2; ++lifetime) {
    const AssociativeCacheStats exact = simulate_associative(events, 4096, 2, lifetime);
    std::cout << "EVAL_CACHE_LIFETIME fixture=" << fixture.name
              << " ways=2 entries=4096"
              << " policy=" << (lifetime == 1 ? "one-root-iteration-including-retries"
                                               : "one-aspiration-attempt")
              << " exact_safe_hits=" << exact.hits << '\n';
  }
  {
    const AssociativeCacheStats exact = simulate_associative(events, 4096, 2);
    std::cout << "EVAL_CACHE_LIFETIME fixture=" << fixture.name
              << " ways=2 entries=4096 policy=full-root-search"
              << " exact_safe_hits=" << exact.hits << '\n';
  }
  std::cout << "EVAL_CACHE_QTT fixture=" << fixture.name
            << " repeated_board_qtt_miss=" << repeated_after_qtt_miss
            << " repeated_board_qtt_hit_no_cutoff=" << repeated_after_qtt_hit
            << " exact_accumulator_repeats_qtt_miss=" << exact_after_qtt_miss
            << " exact_accumulator_repeats_qtt_hit_no_cutoff=" << exact_after_qtt_hit
            << " qtt_window_reusable_but_no_cutoff=" << repeated_after_qtt_window_reusable
            << " prior_iteration_entry=" << repeated_after_qtt_prior_iteration
            << " prior_retry_entry=" << repeated_after_qtt_prior_retry << '\n';

  std::unordered_map<ZobristKey, const EvalReuseRequest*> last_by_key;
  std::size_t tt_ordering_only = 0, tt_miss = 0;
  for (const auto& event : events) {
    if (const auto it = last_by_key.find(event.key); it != last_by_key.end()) {
      if (event.main_tt_state == EvalReuseTtState::Miss) ++tt_miss;
      else if (event.main_tt_state == EvalReuseTtState::HitNoCutoff && event.main_tt_depth >= 0)
        ++tt_ordering_only;
    }
    last_by_key[event.key] = &event;
  }
  std::cout << "EVAL_CACHE_TT fixture=" << fixture.name
            << " repeated_board_main_tt_miss=" << tt_miss
            << " repeated_board_main_tt_hit_no_cutoff=" << tt_ordering_only << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("usage: HebiChessEvalReuseProfile NETWORK");
    std::string error;
    if (!load_nnue_network(argv[1], error)) throw std::runtime_error(error);
    std::vector<Fixture> fixtures(std::begin(kFixtures), std::end(kFixtures));
    std::ifstream corpus("tests/data/wasm-parity-100.fen");
    if (!corpus) throw std::runtime_error("cannot open tests/data/wasm-parity-100.fen");
    std::string line;
    std::size_t line_number = 0;
    std::size_t selected = 0;
    while (std::getline(corpus, line)) {
      if (line.empty() || line[0] == '#') continue;
      if (line_number++ % 6 != 0) continue;
      const std::size_t tab = line.find('\t');
      fixtures.push_back({"corpus-" + std::to_string(selected++),
          tab == std::string::npos ? line : line.substr(tab + 1), 4});
      if (selected == 15) break;
    }
    if (selected != 15) throw std::runtime_error("fewer than 15 sampled corpus positions");
    for (const Fixture& fixture : fixtures) {
      const auto board = Board::from_fen(fixture.fen);
      if (!board) throw std::runtime_error(std::string("invalid fixture: ") + fixture.name);
      clear_transposition_table();
      clear_search_heuristics();
      SearchLimits limits;
      limits.max_depth = fixture.depth;
      limits.eval_mode = EvalMode::NNUE;
      limits.use_root_style_selection = false;
      const auto started = std::chrono::steady_clock::now();
      const SearchResult result = search(*board, limits);
      const double elapsed = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - started).count();
      analyze(fixture, result, elapsed);
    }
    clear_nnue_network();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "eval-reuse-profile: " << error.what() << '\n';
    return 1;
  }
}
