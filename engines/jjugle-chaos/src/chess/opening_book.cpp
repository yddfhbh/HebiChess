#include "chess/opening_book.hpp"

#include <algorithm>
#include <array>
#include <new>

#include "chess/movegen.hpp"
#include "chess/zobrist.hpp"

namespace hebichess {
namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'H', 'E', 'B', 'I', 'B', 'O', 'O', 'K'};
constexpr std::size_t kHeaderSize = 24;
constexpr std::size_t kPositionSize = 16;
constexpr std::size_t kMoveSize = 8;
constexpr std::uint16_t kPromotionMask = 0x7000;
constexpr std::uint16_t kReservedMoveMask = 0x8000;

std::uint16_t read_u16(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t read_u32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t read_u64(const std::uint8_t* p) noexcept {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  return value;
}

bool equal_move(const Move& a, const Move& b) noexcept {
  return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

}  // namespace

ZobristKey opening_book_key(const Board& board) {
  ZobristKey key = board.zobrist_key();
  if (!board.en_passant_target().is_valid()) return key;
  const auto legal = generate_legal_moves(board);
  const bool legal_ep = std::any_of(legal.begin(), legal.end(), [](const Move& move) {
    return move.flag == MoveFlag::EnPassant;
  });
  return legal_ep ? key : key ^ en_passant_zobrist(board.en_passant_target());
}

std::uint16_t OpeningBook::pack_move(const Move& move) noexcept {
  if (!move.from.is_valid() || !move.to.is_valid()) return 0xffff;
  std::uint16_t promotion = 0;
  switch (move.promotion) {
    case PieceType::None: promotion = 0; break;
    case PieceType::Knight: promotion = 1; break;
    case PieceType::Bishop: promotion = 2; break;
    case PieceType::Rook: promotion = 3; break;
    case PieceType::Queen: promotion = 4; break;
    default: return 0xffff;
  }
  return static_cast<std::uint16_t>(move.from.index() |
                                    (move.to.index() << 6) |
                                    (promotion << 12));
}

std::optional<Move> OpeningBook::unpack_move(std::uint16_t packed) noexcept {
  if ((packed & kReservedMoveMask) != 0 || (packed & kPromotionMask) > (4u << 12))
    return std::nullopt;
  const Square from = Square::from_index(static_cast<std::uint8_t>(packed & 0x3f));
  const Square to = Square::from_index(static_cast<std::uint8_t>((packed >> 6) & 0x3f));
  if (!from.is_valid() || !to.is_valid()) return std::nullopt;
  const std::uint8_t promotion = static_cast<std::uint8_t>((packed >> 12) & 7);
  const PieceType type = promotion == 0 ? PieceType::None :
      promotion == 1 ? PieceType::Knight : promotion == 2 ? PieceType::Bishop :
      promotion == 3 ? PieceType::Rook : promotion == 4 ? PieceType::Queen : PieceType::None;
  if (promotion > 4) return std::nullopt;
  return Move{from, to, type, MoveFlag::Normal};
}

void OpeningBook::clear() noexcept {
  positions_.clear();
  moves_.clear();
  max_book_ply_ = min_move_count_ = 0;
}

bool OpeningBook::load(std::span<const std::uint8_t> bytes) noexcept {
  clear();
  try {
  if (bytes.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) return false;
  const std::uint32_t version = read_u32(bytes.data() + 8);
  const std::uint32_t position_count = read_u32(bytes.data() + 12);
  const std::uint32_t move_count = read_u32(bytes.data() + 16);
  if (version != kOpeningBookFormatVersion) return false;
  const std::uint64_t expected = kHeaderSize + static_cast<std::uint64_t>(position_count) * kPositionSize +
                                 static_cast<std::uint64_t>(move_count) * kMoveSize;
  if (expected != bytes.size() || position_count > bytes.size() / kPositionSize ||
      move_count > bytes.size() / kMoveSize || position_count > 10000000U ||
      move_count > 50000000U) return false;
  positions_.reserve(position_count);
  moves_.reserve(move_count);
  std::size_t offset = kHeaderSize;
  std::uint64_t previous_key = 0;
  for (std::uint32_t i = 0; i < position_count; ++i, offset += kPositionSize) {
    const auto* p = bytes.data() + offset;
    const std::uint64_t key = read_u64(p);
    const std::uint32_t move_offset = read_u32(p + 8);
    const std::uint16_t count = read_u16(p + 12);
    if (read_u16(p + 14) != 0 || (i != 0 && key <= previous_key) ||
        static_cast<std::uint64_t>(move_offset) + count > move_count) { clear(); return false; }
    previous_key = key;
    positions_.push_back({key, move_offset, count});
  }
  for (std::uint32_t i = 0; i < move_count; ++i, offset += kMoveSize) {
    const auto* p = bytes.data() + offset;
    const std::uint16_t packed = read_u16(p);
    if (read_u16(p + 2) != 0 || read_u32(p + 4) == 0 || !unpack_move(packed)) { clear(); return false; }
    moves_.push_back({packed, read_u32(p + 4)});
  }
  max_book_ply_ = read_u16(bytes.data() + 20);
  min_move_count_ = read_u16(bytes.data() + 22);
  return true;
  } catch (const std::bad_alloc&) {
    clear();
    return false;
  }
}

std::vector<OpeningBookCandidate> OpeningBook::lookup(const Board& board) const {
  if (positions_.empty()) return {};
  const std::uint64_t key = opening_book_key(board);
  const auto it = std::lower_bound(positions_.begin(), positions_.end(), key,
                                   [](const PositionEntry& entry, std::uint64_t value) { return entry.key < value; });
  if (it == positions_.end() || it->key != key) return {};
  const auto legal = generate_legal_moves(board);
  std::vector<OpeningBookCandidate> result;
  for (std::uint32_t i = 0; i < it->move_count; ++i) {
    const MoveEntry& stored = moves_[it->move_offset + i];
    const auto decoded = unpack_move(stored.packed_move);
    if (!decoded) continue;
    const auto legal_it = std::find_if(legal.begin(), legal.end(), [&](const Move& move) {
      return equal_move(move, *decoded);
    });
    if (legal_it != legal.end()) result.push_back({*legal_it, stored.weight});
  }
  return result;
}

std::optional<Move> OpeningBook::choose_move(const Board& board, std::uint64_t random_value) const {
  const auto candidates = lookup(board);
  std::uint64_t total = 0;
  for (const auto& candidate : candidates) total += candidate.weight;
  if (total == 0) return std::nullopt;
  std::uint64_t bucket = random_value % total;
  for (const auto& candidate : candidates) {
    if (bucket < candidate.weight) return candidate.move;
    bucket -= candidate.weight;
  }
  return std::nullopt;
}

}  // namespace hebichess
