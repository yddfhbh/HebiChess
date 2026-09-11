#include <cassert>
#include <iostream>

#include "chess/game_state.hpp"

using namespace hebichess;

int main() {
  GameState game;
  assert(game.legal_moves().size() == 20);
  std::string san;
  GameStateStatus status;
  assert(game.apply_uci("e2e4", san, status) && san == "e4");
  assert(game.apply_uci("e7e5", san, status) && san == "e5");
  assert(game.apply_uci("g1f3", san, status) && san == "Nf3");

  assert(game.load_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
  assert(game.apply_uci("e1g1", san, status) && san == "O-O");
  assert(game.load_fen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
  assert(!game.apply_uci("e2e5", san, status));
  assert(game.load_fen("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1"));
  status = game.status();
  assert(status.status == "checkmate" && status.result == "1-0");
  assert(game.load_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
  status = game.status();
  assert(status.status == "stalemate" && status.result == "1/2-1/2");
  assert(game.load_fen("8/8/8/8/8/8/8/K6k w - - 0 1"));
  status = game.status();
  assert(status.status == "insufficient material");
  std::cout << "game state ok\n";
}
