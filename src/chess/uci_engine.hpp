#pragma once

#include <functional>
#include <string>

#include "chess/board.hpp"
#include "chess/eval.hpp"

namespace hebichess {

// Stateful UCI command processor shared by the native stdin/stdout program and
// the browser bridge.  Output is delivered one complete line at a time.
class UciEngine {
 public:
  using Output = std::function<void(const std::string&)>;

  explicit UciEngine(Output output);
  void send_command(const std::string& command);

 private:
  Board board_{Board::initial()};
  EvalMode eval_mode_{EvalMode::HCE};
  Output output_;
};

}  // namespace hebichess
