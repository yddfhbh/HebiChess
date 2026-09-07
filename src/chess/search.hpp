#pragma once

#include <cstdint>
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
  std::vector<RootMoveInfo> root_moves{};
};

int evaluate_move_style(const Board& before, const Move& move,
                        const Board& after) noexcept;
bool is_sacrifice_candidate(const Board& before, const Move& move,
                            const Board& after) noexcept;
int negamax(Board& board, int depth, int alpha, int beta, int ply,
            std::uint64_t& nodes);
SearchResult search(const Board& board, int max_depth);

}  // namespace hebichess
