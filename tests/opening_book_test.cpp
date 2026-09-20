#include <cassert>
#include <cstdint>
#include <vector>

#include "chess/opening_book.hpp"

namespace {
using namespace hebichess;

void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
  b.push_back(static_cast<std::uint8_t>(v));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void put64(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

Board board(const char* fen) { return Board::from_fen(fen).value(); }
Move move(const char* from, const char* to, PieceType promotion = PieceType::None) {
  return {Square::from_file_rank(from[0] - 'a', from[1] - '1'),
          Square::from_file_rank(to[0] - 'a', to[1] - '1'), promotion};
}

std::vector<std::uint8_t> fixture(
    std::uint64_t key, const std::vector<std::pair<Move, std::uint32_t>>& moves) {
  std::vector<std::uint8_t> b{'H', 'E', 'B', 'I', 'B', 'O', 'O', 'K'};
  put32(b, 1); put32(b, 1); put32(b, static_cast<std::uint32_t>(moves.size()));
  put16(b, 30); put16(b, 2);
  put64(b, key); put32(b, 0); put16(b, static_cast<std::uint16_t>(moves.size())); put16(b, 0);
  for (const auto& [candidate, weight] : moves) {
    put16(b, OpeningBook::pack_move(candidate)); put16(b, 0); put32(b, weight);
  }
  return b;
}

std::vector<std::uint8_t> two_position_fixture(std::uint64_t first, std::uint64_t second) {
  std::vector<std::uint8_t> b{'H', 'E', 'B', 'I', 'B', 'O', 'O', 'K'};
  put32(b, 1); put32(b, 2); put32(b, 2); put16(b, 30); put16(b, 2);
  put64(b, first); put32(b, 0); put16(b, 1); put16(b, 0);
  put64(b, second); put32(b, 1); put16(b, 1); put16(b, 0);
  const Move candidate = move("e2", "e4");
  put16(b, OpeningBook::pack_move(candidate)); put16(b, 0); put32(b, 1);
  put16(b, OpeningBook::pack_move(candidate)); put16(b, 0); put32(b, 1);
  return b;
}

void expect_reject(OpeningBook& book, const std::vector<std::uint8_t>& bytes) {
  assert(!book.load(bytes));
  assert(book.empty());
}

void test_key_parity_vectors() {
  assert(opening_book_key(board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) == 0xa09a2391da744d6dULL);
  assert(opening_book_key(board("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1")) == 0x1f5c6eaf5d9cfe5eULL);
  assert(opening_book_key(board("rnbqkbnr/pppp1ppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2")) == 0xaa99a98a6f31f223ULL);
  assert(opening_book_key(board("4k3/8/8/8/8/8/8/4K3 w K - 0 1")) == 0x765c25a22747dd88ULL);
  assert(opening_book_key(board("4k3/8/8/8/8/8/8/4K3 w K - 0 1")) != opening_book_key(board("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));
  assert(opening_book_key(board("4k3/8/8/8/8/8/8/4K3 b - e3 0 1")) == 0x5d577a0812f34febULL);
  assert(opening_book_key(board("4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1")) == 0xc9768793264ebbd5ULL);
}

void test_bad_magic() {
  OpeningBook book;
  auto bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes[0] = 'X';
  expect_reject(book, bytes);
}

void test_unsupported_version() {
  OpeningBook book;
  auto bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes[8] = 2;
  expect_reject(book, bytes);
}

void test_truncated_header_and_tables() {
  OpeningBook book;
  auto bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes.resize(23);
  expect_reject(book, bytes);
  bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes.resize(bytes.size() - 1);
  expect_reject(book, bytes);
}

void test_two_positions_and_order_validation() {
  OpeningBook book;
  const auto first = opening_book_key(Board::initial());
  const auto second = first + 1;
  auto bytes = two_position_fixture(first, first);
  expect_reject(book, bytes);  // duplicate position key
  bytes = two_position_fixture(second, first);
  expect_reject(book, bytes);  // descending/out-of-order position key
  bytes = two_position_fixture(first, second);
  assert(book.load(bytes));
}

void test_invalid_offsets_and_reserved_fields() {
  OpeningBook book;
  auto bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes[32] = 0xff;  // invalid move offset
  expect_reject(book, bytes);
  bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes[38] = 1;  // nonzero position reserved field
  expect_reject(book, bytes);
  bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes[42] = 1;  // nonzero move reserved field
  expect_reject(book, bytes);
}

void test_invalid_packed_move_and_zero_weight() {
  OpeningBook book;
  auto bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  bytes[40] = 0xff; bytes[41] = 0xff;
  expect_reject(book, bytes);
  bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 0}});
  expect_reject(book, bytes);
}

void test_lookup_filtering_and_weighted_boundaries() {
  const Board initial = Board::initial();
  const Move a = move("e2", "e4"), b = move("d2", "d4"), c = move("c2", "c4");
  const Move illegal = move("a1", "a8");
  OpeningBook book;
  auto bytes = fixture(opening_book_key(initial), {{a, 3}, {b, 2}, {c, 5}, {illegal, 0xffffffffU}});
  assert(book.load(bytes) && book.lookup(initial).size() == 3);
  assert(book.choose_move(initial, 0)->to == a.to);
  assert(book.choose_move(initial, 2)->to == a.to);
  assert(book.choose_move(initial, 3)->to == b.to);
  assert(book.choose_move(initial, 4)->to == b.to);
  assert(book.choose_move(initial, 5)->to == c.to);
  assert(book.choose_move(initial, 9)->to == c.to);
  bytes = fixture(opening_book_key(initial), {{illegal, 1}});
  assert(book.load(bytes) && book.lookup(initial).empty() && !book.choose_move(initial, 0));
  bytes = fixture(opening_book_key(initial), {{a, 0xffffffffU}});
  assert(book.load(bytes) && book.choose_move(initial, 0xffffffffU - 1)->from == a.from);
}

void test_clear_and_reload() {
  OpeningBook book;
  const auto bytes = fixture(opening_book_key(Board::initial()), {{move("e2", "e4"), 1}});
  assert(book.load(bytes) && book.available());
  book.clear();
  assert(book.empty());
  assert(book.load(bytes) && book.available());
}

void test_pack_roundtrip() {
  for (PieceType p : {PieceType::None, PieceType::Knight, PieceType::Bishop,
                      PieceType::Rook, PieceType::Queen}) {
    const Move m = move("e7", "e8", p);
    const auto decoded = OpeningBook::unpack_move(OpeningBook::pack_move(m));
    assert(decoded && decoded->from == m.from && decoded->to == m.to && decoded->promotion == p);
  }
  assert(!OpeningBook::unpack_move(0x8000));
}
}  // namespace

int main() {
  test_key_parity_vectors();
  test_bad_magic();
  test_unsupported_version();
  test_truncated_header_and_tables();
  test_two_positions_and_order_validation();
  test_invalid_offsets_and_reserved_fields();
  test_invalid_packed_move_and_zero_weight();
  test_lookup_filtering_and_weighted_boundaries();
  test_clear_and_reload();
  test_pack_roundtrip();
}
