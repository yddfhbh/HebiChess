#include "chess/uci.hpp"

#include <cctype>

#include "chess/movegen.hpp"

namespace hebichess {
namespace {

std::optional<PieceType> promotion_piece(char symbol) {
  switch (static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)))) {
    case 'q': return PieceType::Queen;
    case 'r': return PieceType::Rook;
    case 'b': return PieceType::Bishop;
    case 'n': return PieceType::Knight;
    default: return std::nullopt;
  }
}

bool parse_square(const std::string& text, std::size_t offset, Square& square) {
  if (offset + 1 >= text.size() || text[offset] < 'a' || text[offset] > 'h' ||
      text[offset + 1] < '1' || text[offset + 1] > '8') return false;
  square = Square::from_file_rank(static_cast<std::uint8_t>(text[offset] - 'a'),
                                  static_cast<std::uint8_t>(text[offset + 1] - '1'));
  return square.is_valid();
}

}  // namespace

std::optional<Move> parse_uci_move(const Board& board, const std::string& text) {
  if (text.size() != 4 && text.size() != 5) return std::nullopt;
  Square from, to;
  if (!parse_square(text, 0, from) || !parse_square(text, 2, to)) return std::nullopt;
  PieceType promotion = PieceType::None;
  if (text.size() == 5) {
    const auto parsed = promotion_piece(text[4]);
    if (!parsed) return std::nullopt;
    promotion = *parsed;
  }
  for (const Move& move : generate_legal_moves(board)) {
    if (move.from == from && move.to == to && move.promotion == promotion &&
        (promotion != PieceType::None || !move.is_promotion())) return move;
  }
  return std::nullopt;
}

std::string move_to_uci(const Move& move) {
  if (!move.from.is_valid() || !move.to.is_valid()) return "0000";
  std::string result;
  result += static_cast<char>('a' + move.from.file());
  result += static_cast<char>('1' + move.from.rank());
  result += static_cast<char>('a' + move.to.file());
  result += static_cast<char>('1' + move.to.rank());
  if (move.is_promotion()) {
    switch (move.promotion) {
      case PieceType::Queen: result += 'q'; break;
      case PieceType::Rook: result += 'r'; break;
      case PieceType::Bishop: result += 'b'; break;
      case PieceType::Knight: result += 'n'; break;
      default: break;
    }
  }
  return result;
}

}  // namespace hebichess
