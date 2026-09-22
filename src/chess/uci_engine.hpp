#pragma once

#include <functional>
#include <cstdint>
#include <span>
#include <string>

#include "chess/board.hpp"
#include "chess/eval.hpp"
#include "chess/opening_book.hpp"

namespace hebichess {

// Stateful UCI command processor shared by the native stdin/stdout program and
// the browser bridge.  Output is delivered one complete line at a time.
class UciEngine {
 public:
  using Output = std::function<void(const std::string&)>;

  explicit UciEngine(Output output);
  void send_command(const std::string& command);

  bool load_opening_book_bytes(std::span<const std::uint8_t> bytes);
  void clear_opening_book() noexcept;
  bool opening_book_available() const noexcept;

 private:
  std::uint64_t next_book_random();
  void reset_book_random_state() noexcept;

  Board board_{Board::initial()};
  EvalMode eval_mode_{EvalMode::HCE};
  int max_move_time_ms_{0};
  OpeningBook opening_book_;
  bool own_book_{false};
  std::uint64_t book_seed_{0};
  std::uint64_t book_game_counter_{0};
  std::uint64_t book_random_state_{0};
  Output output_;
};

}  // namespace hebichess
