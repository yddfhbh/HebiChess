#include <iostream>
#include <string>

#include "chess/uci_engine.hpp"

int main() {
  hebichess::UciEngine engine([](const std::string& line) {
    std::cout << line << std::endl;
  });
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "quit") break;
    engine.send_command(line);
  }
}
