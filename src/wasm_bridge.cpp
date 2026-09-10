#include <string>
#include <utility>

#include "chess/uci_engine.hpp"

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

const char* hebichess_take_output() {
  return output.c_str();
}

}
