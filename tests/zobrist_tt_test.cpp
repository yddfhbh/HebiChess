#include <cassert>
#include <chrono>
#include <iostream>

#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/uci.hpp"

using namespace hebichess;

namespace {

Board board(const char* fen) { return Board::from_fen(fen).value(); }

void assert_integrity(Board position) {
  const ZobristKey before = position.zobrist_key();
  const std::string fen = position.to_fen();
  for (const Move& move : generate_legal_moves(position)) {
    const UndoState undo = position.make_move(move);
    assert(position.zobrist_key() == compute_zobrist(position));
    position.unmake_move(move, undo);
    assert(position.zobrist_key() == before);
    assert(position.to_fen() == fen);
  }
}

void test_position_identity() {
  const Board initial = Board::initial();
  assert(initial.zobrist_key() == Board::initial().zobrist_key());
  assert(initial.zobrist_key() == compute_zobrist(initial));
  assert(initial.zobrist_key() != board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1").zobrist_key());
  assert(initial.zobrist_key() != board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1").zobrist_key());
  assert(initial.zobrist_key() != board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKbnr w KQkq - 0 1").zobrist_key());
  assert(initial.zobrist_key() != board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e3 0 1").zobrist_key());
  assert(initial.zobrist_key() == board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 37 99").zobrist_key());
}

void test_special_moves() {
  const char* fens[] = {
      "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", // normal/double pawn push
      "6k1/8/8/8/8/8/3q4/3QK3 w - - 0 1", // capture
      "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", // en passant
      "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", // both castlings
      "4k3/P7/8/8/8/8/8/4K3 w - - 0 1", // promotion
      "1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1", // promotion capture
  };
  for (const char* fen : fens) assert_integrity(board(fen));
}

void test_null_move_integrity() {
  Board position = board("r3k2r/8/8/3pP3/8/8/8/R3K2R b KQkq e3 17 42");
  const std::string fen = position.to_fen();
  const ZobristKey key = position.zobrist_key();
  const NullUndoState undo = position.make_null_move();
  assert(position.side_to_move() == Color::White);
  assert(!position.en_passant_target().is_valid());
  assert(position.zobrist_key() == compute_zobrist(position));
  position.unmake_null_move(undo);
  assert(position.zobrist_key() == key);
  assert(position.to_fen() == fen);
}

void test_tt_consistency() {
  const Board position = board("r2q1rk1/ppp1bppp/2np4/8/2B1P3/2N1BN2/PPP2PPP/R2Q1RK1 w - - 0 1");
  SearchLimits off;
  off.max_depth = 3;
  off.use_tt = false;
  const auto off_started = std::chrono::steady_clock::now();
  const SearchResult without_tt = search(position, off);
  const auto off_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - off_started).count();
  clear_transposition_table();
  SearchLimits on = off;
  on.use_tt = true;
  const auto started = std::chrono::steady_clock::now();
  const SearchResult with_tt = search(position, on);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  assert(without_tt.best_move == with_tt.best_move);
  assert(without_tt.score == with_tt.score);
  assert(with_tt.tt_probes > 0);
  std::cout << "tt off bestmove " << move_to_uci(without_tt.best_move)
            << " score " << without_tt.score << " nodes " << without_tt.nodes
            << " qnodes " << without_tt.qnodes << " elapsed_ms " << off_elapsed << '\n'
            << "tt on bestmove " << move_to_uci(with_tt.best_move)
            << " score " << with_tt.score << " nodes " << with_tt.nodes
            << " qnodes " << with_tt.qnodes << " tt_probes " << with_tt.tt_probes
            << " tt_hits " << with_tt.tt_hits << " tt_cutoffs " << with_tt.tt_cutoffs
            << " elapsed_ms " << elapsed << '\n';
}

}  // namespace

int main() {
  test_position_identity();
  test_special_moves();
  test_null_move_integrity();
  test_tt_consistency();
}
