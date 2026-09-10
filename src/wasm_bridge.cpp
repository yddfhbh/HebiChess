#include <string>
#include <utility>

#include "chess/uci_engine.hpp"

namespace {

std::string output;
hebichess::UciEngine& engine() {
  static hebichess::UciEngine instance([](const std::string& line) {
    output += line;
    output += '\n';
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
