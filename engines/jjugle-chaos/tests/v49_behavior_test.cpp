#include <iostream>
#include <string>
#include <vector>
#include "chess/board.hpp"
#include "chess/eval.hpp"
#include "chess/uci_engine.hpp"

using namespace hebichess;

int main() {
  if (evaluate_chaos_preparation(Board::initial(), Color::White) != 0) {
    std::cerr << "UNEXPECTED_OPENING_PREPARATION\n";
    return 1;
  }
  std::vector<std::string> lines;
  UciEngine engine([&lines](const std::string& text) { lines.push_back(text); });
  engine.send_command("ucinewgame");
  engine.send_command("position startpos moves g1f3 g8f6 f3g1 f6g8");
  engine.send_command("go depth 3");
  bool history_four_plies = false;
  bool legal_move = false;
  for (const auto& line : lines) {
    if (line.find("v49 history_positions 5") != std::string::npos)
      history_four_plies = true;
    if (line.rfind("bestmove ", 0) == 0) legal_move = true;
  }
  if (!history_four_plies || !legal_move) {
    std::cerr << "UCI_HISTORY_MISSING\n";
    return 1;
  }
  lines.clear();
  engine.send_command("position startpos");
  engine.send_command("go depth 2");
  bool history_reset = false;
  for (const auto& line : lines)
    if (line.find("v49 history_positions 1") != std::string::npos)
      history_reset = true;
  if (!history_reset) {
    std::cerr << "UCI_HISTORY_RESET_FAILED\n";
    return 1;
  }
  std::cout << "V49_UCI_HISTORY_TEST=PASS\n";
  return 0;
}
