#include <cassert>
#include <cstdint>
#include <vector>

#include "chess/opening_book.hpp"

namespace {
using namespace hebichess;
void put16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(v); b.push_back(v >> 8); }
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(v >> (8 * i)); }
void put64(std::vector<std::uint8_t>& b, std::uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(v >> (8 * i)); }
std::vector<std::uint8_t> fixture(std::uint64_t key, const std::vector<std::pair<Move, std::uint32_t>>& moves) {
  std::vector<std::uint8_t> b{'H','E','B','I','B','O','O','K'};
  put32(b, 1); put32(b, 1); put32(b, static_cast<std::uint32_t>(moves.size())); put16(b, 30); put16(b, 2);
  put64(b, key); put32(b, 0); put16(b, moves.size()); put16(b, 0);
  for (const auto& [move, weight] : moves) { put16(b, OpeningBook::pack_move(move)); put16(b, 0); put32(b, weight); }
  return b;
}
Board board(const char* fen) { return Board::from_fen(fen).value(); }
Move move(const char* from, const char* to, PieceType promotion = PieceType::None) { return {Square::from_file_rank(from[0] - 'a', from[1] - '1'), Square::from_file_rank(to[0] - 'a', to[1] - '1'), promotion}; }
void test_vectors_and_ep() {
  assert(opening_book_key(board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) == 0xa09a2391da744d6dULL);
  assert(opening_book_key(board("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1")) == 0x1f5c6eaf5d9cfe5eULL);
  assert(opening_book_key(board("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1")) == 0x1f5c6eaf5d9cfe5eULL);
  assert(opening_book_key(board("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b - - 0 1")) == 0x6c5deea1707f8124ULL);
  assert(opening_book_key(board("4k3/8/8/8/8/8/4P3/4K3 b - e3 0 1")) == opening_book_key(board("4k3/8/8/8/8/8/4P3/4K3 b - - 0 1")));
  assert(opening_book_key(board("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1")) == 0xc9768793264ebbd5ULL);
  assert(opening_book_key(board("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1")) != opening_book_key(board("4k3/8/8/8/3pP3/8/8/4K3 b - - 0 1")));
  assert(opening_book_key(board("4k3/8/8/8/8/8/8/4K3 w K - 0 1")) != opening_book_key(board("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));
}
void test_book() {
  const Board initial = Board::initial(); const Move a = move("e2", "e4"), b = move("d2", "d4"), c = move("c2", "c4"), illegal = move("a1", "a8"); OpeningBook book;
  auto bytes = fixture(opening_book_key(initial), {{a, 3}, {b, 2}, {c, 5}, {illegal, 0xffffffffU}});
  assert(book.load(bytes) && book.lookup(initial).size() == 3); assert(book.choose_move(initial, 0)->to == a.to); assert(book.choose_move(initial, 2)->to == a.to); assert(book.choose_move(initial, 3)->to == b.to); assert(book.choose_move(initial, 4)->to == b.to); assert(book.choose_move(initial, 5)->to == c.to); assert(book.choose_move(initial, 9)->to == c.to);
  auto large = fixture(opening_book_key(initial), {{a, 0xffffffffU}}); assert(book.load(large) && book.choose_move(initial, 0xffffffffU - 1)->from == a.from);
  auto all_illegal = fixture(opening_book_key(initial), {{illegal, 1}}); assert(book.load(all_illegal) && book.lookup(initial).empty() && !book.choose_move(initial, 0));
  bytes = fixture(opening_book_key(initial), {{a, 3}, {b, 2}, {c, 5}, {illegal, 1}});
  auto bad = bytes; bad.resize(bad.size() - 1); assert(!book.load(bad) && book.empty()); bad = bytes; bad[0] = 'X'; assert(!book.load(bad)); bad = bytes; bad[8] = 2; assert(!book.load(bad)); bad = bytes; bad[32] = 0xff; assert(!book.load(bad)); bad = bytes; bad[38] = 1; assert(!book.load(bad)); bad = bytes; bad[42] = 1; assert(!book.load(bad)); bytes = fixture(opening_book_key(initial), {{a, 0}}); assert(!book.load(bytes)); book.clear(); assert(book.empty());
}
void test_pack_roundtrip() { for (PieceType p : {PieceType::None, PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) { const Move m = move("e7", "e8", p); const auto d = OpeningBook::unpack_move(OpeningBook::pack_move(m)); assert(d && d->from == m.from && d->to == m.to && d->promotion == p); } assert(!OpeningBook::unpack_move(0x8000)); }
}  // namespace
int main() { test_vectors_and_ep(); test_pack_roundtrip(); test_book(); }
