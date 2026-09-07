#pragma once

#include <cstdint>
#include <functional>
#include <chrono>
#include <vector>

#include "chess/eval.hpp"
#include "chess/movegen.hpp"

namespace hebichess {

constexpr int MATE_SCORE = 30000;

struct RootMoveInfo {
  Move move{};
  int search_score{0};
  int style_score{0};
  bool sacrifice_candidate{false};
};

struct SearchResult {
  Move best_move{};
  int score{0};
  std::uint64_t nodes{0};
  std::uint64_t qnodes{0};
  std::vector<RootMoveInfo> root_moves{};
};

struct SearchLimits {
  int max_depth{1};
  bool has_deadline{false};
  std::chrono::steady_clock::time_point deadline{};
};

using SearchInfoCallback = std::function<void(int, int, std::uint64_t,
                                               std::uint64_t)>;

int evaluate_move_style(const Board& before, const Move& move,
                        const Board& after) noexcept;
bool is_sacrifice_candidate(const Board& before, const Move& move,
                            const Board& after) noexcept;
int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes);
int quiescence(Board& board, int alpha, int beta, int ply);
SearchResult search(const Board& board, int max_depth);
SearchResult search(const Board& board, const SearchLimits& limits,
                    const SearchInfoCallback& on_iteration = {});

}  // namespace hebichess
