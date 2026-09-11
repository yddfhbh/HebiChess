#include <string>
#include <utility>

#include "chess/uci_engine.hpp"
#include "chess/game_state.hpp"

#ifdef HEBICHESS_BROWSER_WASM
#include <emscripten.h>

EM_JS(void, hebichess_browser_output, (const char* line), {
  if (typeof self.hebichessOutputLine === 'function') {
    self.hebichessOutputLine(UTF8ToString(line));
  }
});
#endif

namespace {

std::string output;
hebichess::GameState game_state;
hebichess::UciEngine& engine() {
  static hebichess::UciEngine instance([](const std::string& line) {
    output += line;
    output += '\n';
#ifdef HEBICHESS_BROWSER_WASM
    hebichess_browser_output(line.c_str());
#endif
  });
  return instance;
}

}  // namespace

extern "C" {

void hebichess_initialize() {
  output.clear();
  engine().send_command("uci");
  engine().send_command("isready");
}

void hebichess_send_command(const char* command) {
  output.clear();
  if (!command) {
    output = "info string error empty command\n";
    return;
  }
  engine().send_command(command);
}

void hebichess_game_reset() { game_state.reset(); }
int hebichess_game_load_fen(const char* fen) { return fen && game_state.load_fen(fen) ? 1 : 0; }
const char* hebichess_game_fen() { output = game_state.fen(); return output.c_str(); }
const char* hebichess_game_legal_moves() {
  output = "[";
  const auto moves = game_state.legal_moves();
  for (std::size_t i = 0; i < moves.size(); ++i) { if (i) output += ','; output += '"' + moves[i] + '"'; }
  output += ']'; return output.c_str();
}
const char* hebichess_game_apply(const char* move) {
  std::string san; hebichess::GameStateStatus status;
  if (!move || !game_state.apply_uci(move, san, status)) { output = "{\"ok\":false}"; return output.c_str(); }
  output = "{\"ok\":true,\"fen\":\"" + game_state.fen() + "\",\"san\":\"" + san +
           "\",\"status\":\"" + status.status + "\",\"result\":\"" + status.result + "\"}";
  return output.c_str();
}
const char* hebichess_game_status() {
  const auto status = game_state.status();
  output = "{\"status\":\"" + status.status + "\",\"result\":\"" + status.result + "\"}";
  return output.c_str();
}

const char* hebichess_take_output() {
  return output.c_str();
}

}
