#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "chess/board.hpp"
#include "chess/movegen.hpp"

using namespace hebichess;

namespace {

constexpr std::uint64_t kSeed = 0x9e3779b97f4a7c15ULL;

std::uint64_t next(std::uint64_t& state) {
  state ^= state << 7;
  state ^= state >> 9;
  return state;
}

bool add(std::vector<std::string>& corpus, std::set<std::string>& seen,
         const Board& board) {
  const auto fen = board.to_fen();
  if (seen.insert(fen).second) {
    corpus.push_back(fen);
    return true;
  }
  return false;
}

}  // namespace

int main() {
  // These three positions guarantee coverage of castling, en passant, and
  // promotion while remaining non-terminal legal positions.
  std::vector<std::string> corpus;
  std::set<std::string> seen;
  for (const auto& fen : {
           std::string("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"),
           std::string("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"),
           std::string("4k3/P7/8/8/8/8/8/4K3 w - - 0 1")}) {
    const auto board = Board::from_fen(fen);
    if (!board || generate_legal_moves(*board).empty()) return 2;
    add(corpus, seen, *board);
  }

  // The remaining entries are deterministic positions from one seeded legal
  // random walk.  Sampling early, middle, and late plies gives a stable mix
  // of opening, middlegame, and endgame material without external libraries.
  std::uint64_t state = kSeed;
  Board board = Board::initial();
  const std::array<int, 97> sample_plies = [] {
    std::array<int, 97> result{};
    for (int i = 0; i < 97; ++i) result[static_cast<std::size_t>(i)] = 2 + i * 2;
    return result;
  }();
  int next_sample = 0;
  for (int ply = 1; next_sample < static_cast<int>(sample_plies.size()) && ply <= 260; ++ply) {
    auto moves = generate_legal_moves(board);
    if (moves.empty()) {
      board = Board::initial();
      state ^= 0xd1b54a32d192ed03ULL;
      ply = 0;
      continue;
    }
    board.make_move(moves[next(state) % moves.size()]);
    if (ply == sample_plies[static_cast<std::size_t>(next_sample)]) {
      if (add(corpus, seen, board)) ++next_sample;
      else ++next_sample;
    }
  }
  if (corpus.size() != 100) return 3;

  std::cout << "# HebiChess canonical WASM parity corpus\n"
            << "# seed: 0x9e3779b97f4a7c15 (xorshift64; legal move index sampling)\n"
            << "# generation: 3 fixed special positions + 97 positions sampled every 2 plies\n"
            << "# FEN count: 100\n";
  for (const auto& fen : corpus) std::cout << fen << '\n';
}
