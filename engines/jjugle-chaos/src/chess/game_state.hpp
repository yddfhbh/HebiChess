#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "chess/board.hpp"

namespace hebichess {

struct GameStateStatus {
  std::string status{"ongoing"};
  std::string result;
  Square check_square{};
};

class GameState {
 public:
  GameState();

  void reset();
  bool load_fen(const std::string& fen);
  const Board& board() const noexcept { return board_; }
  std::string fen() const { return board_.to_fen(); }
  std::vector<std::string> legal_moves() const;
  bool apply_uci(const std::string& uci, std::string& san, GameStateStatus& status);
  GameStateStatus status() const;

 private:
  std::string position_key() const;
  Board board_;
  std::unordered_map<std::string, unsigned> repetitions_;
};

std::string san_for_move(Board& board, const Move& move);
GameStateStatus terminal_status(Board& board,
                                const std::unordered_map<std::string, unsigned>& repetitions);

}  // namespace hebichess
