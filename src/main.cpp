#include <iostream>

#include "chess/board.hpp"

int main() {
  const hebichess::Board board;
  std::cout << "HebiChess (" << board.squares().size() << " squares)\n";
  return 0;
}
