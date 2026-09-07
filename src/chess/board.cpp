#include "chess/board.hpp"

#include <charconv>
#include <sstream>
#include <string_view>

namespace hebichess {
namespace {

constexpr Piece white(PieceType type) noexcept { return {type, Color::White}; }
constexpr Piece black(PieceType type) noexcept { return {type, Color::Black}; }

Piece piece_from_fen(char symbol) noexcept {
  const Color color = symbol >= 'a' && symbol <= 'z' ? Color::Black
                                                        : Color::White;
  switch (symbol >= 'a' && symbol <= 'z' ? symbol - 'a' : symbol - 'A') {
    case 'P' - 'A':
      return {PieceType::Pawn, color};
    case 'N' - 'A':
      return {PieceType::Knight, color};
    case 'B' - 'A':
      return {PieceType::Bishop, color};
    case 'R' - 'A':
      return {PieceType::Rook, color};
    case 'Q' - 'A':
      return {PieceType::Queen, color};
    case 'K' - 'A':
      return {PieceType::King, color};
    default:
      return {};
  }
}

char piece_to_fen(Piece piece) noexcept {
  char symbol = '?';
  switch (piece.type) {
    case PieceType::Pawn:
      symbol = 'p';
      break;
    case PieceType::Knight:
      symbol = 'n';
      break;
    case PieceType::Bishop:
      symbol = 'b';
      break;
    case PieceType::Rook:
      symbol = 'r';
      break;
    case PieceType::Queen:
      symbol = 'q';
      break;
    case PieceType::King:
      symbol = 'k';
      break;
    case PieceType::None:
      return '?';
  }
  return piece.color == Color::White ? static_cast<char>(symbol - 'a' + 'A')
                                     : symbol;
}

bool parse_uint(std::string_view text, std::uint32_t& value) noexcept {
  if (text.empty()) {
    return false;
  }
  const char* first = text.data();
  const char* last = first + text.size();
  const auto result = std::from_chars(first, last, value);
  return result.ec == std::errc{} && result.ptr == last;
}

bool parse_square(std::string_view text, Square& square) noexcept {
  if (text.size() != 2 || text[0] < 'a' || text[0] > 'h' ||
      text[1] < '1' || text[1] > '8') {
    return false;
  }
  square = Square::from_file_rank(static_cast<std::uint8_t>(text[0] - 'a'),
                                  static_cast<std::uint8_t>(text[1] - '1'));
  return square.is_valid();
}

bool parse_castling(std::string_view text, CastlingRights& rights) noexcept {
  rights = {};
  if (text == "-") {
    return true;
  }
  if (text.empty()) {
    return false;
  }
  for (const char symbol : text) {
    bool* right = nullptr;
    switch (symbol) {
      case 'K':
        right = &rights.white_king_side;
        break;
      case 'Q':
        right = &rights.white_queen_side;
        break;
      case 'k':
        right = &rights.black_king_side;
        break;
      case 'q':
        right = &rights.black_queen_side;
        break;
      default:
        return false;
    }
    if (*right) {
      return false;
    }
    *right = true;
  }
  return true;
}

bool parse_piece_placement(std::string_view text, Board& board) noexcept {
  std::size_t rank = 7;
  std::size_t file = 0;
  for (const char symbol : text) {
    if (symbol == '/') {
      if (file != Square::kBoardSide || rank == 0) {
        return false;
      }
      --rank;
      file = 0;
      continue;
    }

    if (symbol >= '1' && symbol <= '8') {
      file += static_cast<std::size_t>(symbol - '0');
    } else {
      const Piece piece = piece_from_fen(symbol);
      if (piece.is_empty()) {
        return false;
      }
      ++file;
      if (file > Square::kBoardSide) {
        return false;
      }
      board.set_piece(Square::from_file_rank(
                          static_cast<std::uint8_t>(file - 1),
                          static_cast<std::uint8_t>(rank)),
                      piece);
    }
    if (file > Square::kBoardSide) {
      return false;
    }
  }
  return rank == 0 && file == Square::kBoardSide;
}

void remove_castling_for_rook_square(CastlingRights& rights, Square square) noexcept {
  if (square == Square::from_file_rank(0, 0)) rights.white_queen_side = false;
  if (square == Square::from_file_rank(7, 0)) rights.white_king_side = false;
  if (square == Square::from_file_rank(0, 7)) rights.black_queen_side = false;
  if (square == Square::from_file_rank(7, 7)) rights.black_king_side = false;
}

void place_back_rank(Board& board, std::uint8_t rank, Color color) noexcept {
  Piece (*piece_for_file)(PieceType) noexcept =
      color == Color::White ? white : black;

  constexpr PieceType back_rank[] = {
      PieceType::Rook, PieceType::Knight, PieceType::Bishop, PieceType::Queen,
      PieceType::King, PieceType::Bishop, PieceType::Knight, PieceType::Rook,
  };

  for (std::uint8_t file = 0; file < Square::kBoardSide; ++file) {
    board.set_piece(Square::from_file_rank(file, rank),
                    piece_for_file(back_rank[file]));
  }
}

}  // namespace

Board::Board() noexcept {
  squares_.fill({});
  place_back_rank(*this, 0, Color::White);
  place_back_rank(*this, 7, Color::Black);

  for (std::uint8_t file = 0; file < Square::kBoardSide; ++file) {
    set_piece(Square::from_file_rank(file, 1), white(PieceType::Pawn));
    set_piece(Square::from_file_rank(file, 6), black(PieceType::Pawn));
  }
}

Board Board::initial() noexcept {
  return Board{};
}

bool Board::is_square_attacked(Square square, Color by_color) const noexcept {
  if (!square.is_valid()) return false;
  const int file = square.file();
  const int rank = square.rank();
  const int pawn_rank = rank + (by_color == Color::White ? -1 : 1);
  for (const int df : {-1, 1}) {
    const Square source = Square::from_file_rank(static_cast<std::uint8_t>(file + df),
                                                static_cast<std::uint8_t>(pawn_rank));
    if (source.is_valid() && piece_at(source) == Piece{PieceType::Pawn, by_color}) return true;
  }
  constexpr int knight_steps[][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
  for (const auto& step : knight_steps) {
    const Square source = Square::from_file_rank(static_cast<std::uint8_t>(file + step[0]),
                                                static_cast<std::uint8_t>(rank + step[1]));
    if (source.is_valid() && piece_at(source) == Piece{PieceType::Knight, by_color}) return true;
  }
  constexpr int directions[][3] = {{1, 0, 1}, {-1, 0, 1}, {0, 1, 1}, {0, -1, 1},
                                   {1, 1, 2}, {1, -1, 2}, {-1, 1, 2}, {-1, -1, 2}};
  for (const auto& direction : directions) {
    for (int f = file + direction[0], r = rank + direction[1]; f >= 0 && f < 8 && r >= 0 && r < 8;
         f += direction[0], r += direction[1]) {
      const Piece piece = piece_at(Square::from_file_rank(f, r));
      if (!piece.is_empty()) {
        if (piece.color == by_color &&
            (piece.type == PieceType::Queen || piece.type == (direction[2] == 1 ? PieceType::Rook : PieceType::Bishop))) return true;
        break;
      }
    }
  }
  constexpr int king_steps[][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
  for (const auto& step : king_steps) {
    const Square source = Square::from_file_rank(static_cast<std::uint8_t>(file + step[0]),
                                                static_cast<std::uint8_t>(rank + step[1]));
    if (source.is_valid() && piece_at(source) == Piece{PieceType::King, by_color}) return true;
  }
  return false;
}

Square Board::find_king(Color color) const noexcept {
  for (std::uint8_t index = 0; index < Square::kSquareCount; ++index) {
    const Square square = Square::from_index(index);
    if (piece_at(square) == Piece{PieceType::King, color}) return square;
  }
  return {};
}

UndoState Board::make_move(const Move& move) noexcept {
  UndoState undo{piece_at(move.to), move.to, castling_rights_,
                  en_passant_target_, halfmove_clock_, fullmove_number_,
                  side_to_move_};
  const Piece moving_piece = piece_at(move.from);
  const Color moving_color = moving_piece.color;

  if (move.flag == MoveFlag::EnPassant) {
    undo.captured_square = Square::from_file_rank(move.to.file(), move.from.rank());
    undo.captured_piece = piece_at(undo.captured_square);
    set_piece(undo.captured_square, {});
  }
  set_piece(move.from, {});
  Piece placed = moving_piece;
  if (move.is_promotion()) placed.type = move.promotion;
  set_piece(move.to, placed);

  if (move.flag == MoveFlag::CastleKingSide) {
    const Square rook_from = Square::from_file_rank(7, move.from.rank());
    const Square rook_to = Square::from_file_rank(5, move.from.rank());
    set_piece(rook_to, piece_at(rook_from));
    set_piece(rook_from, {});
  } else if (move.flag == MoveFlag::CastleQueenSide) {
    const Square rook_from = Square::from_file_rank(0, move.from.rank());
    const Square rook_to = Square::from_file_rank(3, move.from.rank());
    set_piece(rook_to, piece_at(rook_from));
    set_piece(rook_from, {});
  }

  if (moving_piece.type == PieceType::King) {
    if (moving_color == Color::White) {
      castling_rights_.white_king_side = false;
      castling_rights_.white_queen_side = false;
    } else {
      castling_rights_.black_king_side = false;
      castling_rights_.black_queen_side = false;
    }
  }
  if (moving_piece.type == PieceType::Rook) {
    remove_castling_for_rook_square(castling_rights_, move.from);
  }
  if (undo.captured_piece.type == PieceType::Rook) {
    remove_castling_for_rook_square(castling_rights_, undo.captured_square);
  }

  en_passant_target_ = {};
  if (move.flag == MoveFlag::DoublePawnPush) {
    en_passant_target_ = Square::from_file_rank(
        move.from.file(), static_cast<std::uint8_t>((move.from.rank() + move.to.rank()) / 2));
  }
  halfmove_clock_ = moving_piece.type == PieceType::Pawn ||
                           !undo.captured_piece.is_empty()
                       ? 0
                       : halfmove_clock_ + 1;
  if (moving_color == Color::Black) ++fullmove_number_;
  side_to_move_ = opposite(side_to_move_);
  return undo;
}

void Board::unmake_move(const Move& move, const UndoState& undo) noexcept {
  side_to_move_ = undo.side_to_move;
  castling_rights_ = undo.castling_rights;
  en_passant_target_ = undo.en_passant_target;
  halfmove_clock_ = undo.halfmove_clock;
  fullmove_number_ = undo.fullmove_number;

  Piece restored = piece_at(move.to);
  if (move.is_promotion()) restored.type = PieceType::Pawn;
  set_piece(move.from, restored);
  set_piece(move.to, {});
  if (move.flag == MoveFlag::CastleKingSide) {
    const Square rook_from = Square::from_file_rank(7, move.from.rank());
    const Square rook_to = Square::from_file_rank(5, move.from.rank());
    set_piece(rook_from, piece_at(rook_to));
    set_piece(rook_to, {});
  } else if (move.flag == MoveFlag::CastleQueenSide) {
    const Square rook_from = Square::from_file_rank(0, move.from.rank());
    const Square rook_to = Square::from_file_rank(3, move.from.rank());
    set_piece(rook_from, piece_at(rook_to));
    set_piece(rook_to, {});
  }
  if (undo.captured_piece.is_empty()) {
    if (move.flag == MoveFlag::EnPassant) set_piece(undo.captured_square, {});
  } else {
    set_piece(undo.captured_square, undo.captured_piece);
  }
}

UndoState make_move(Board& board, const Move& move) noexcept {
  return board.make_move(move);
}

void unmake_move(Board& board, const Move& move, const UndoState& undo) noexcept {
  board.unmake_move(move, undo);
}

std::optional<Board> Board::from_fen(const std::string& fen) {
  std::istringstream input(fen);
  std::string placement;
  std::string active_color;
  std::string castling;
  std::string en_passant;
  std::string halfmove;
  std::string fullmove;
  std::string extra;
  if (!(input >> placement >> active_color >> castling >> en_passant >>
        halfmove >> fullmove) || input >> extra) {
    return std::nullopt;
  }

  Board board;
  board.squares_.fill({});
  if (!parse_piece_placement(placement, board)) {
    return std::nullopt;
  }
  if (active_color == "w") {
    board.side_to_move_ = Color::White;
  } else if (active_color == "b") {
    board.side_to_move_ = Color::Black;
  } else {
    return std::nullopt;
  }
  if (!parse_castling(castling, board.castling_rights_)) {
    return std::nullopt;
  }
  if (en_passant == "-") {
    board.en_passant_target_ = Square{};
  } else if (!parse_square(en_passant, board.en_passant_target_) ||
             (board.en_passant_target_.rank() != 2 &&
              board.en_passant_target_.rank() != 5)) {
    return std::nullopt;
  }
  if (!parse_uint(halfmove, board.halfmove_clock_) ||
      !parse_uint(fullmove, board.fullmove_number_) ||
      board.fullmove_number_ == 0) {
    return std::nullopt;
  }
  return board;
}

std::string Board::to_fen() const {
  std::string fen;
  for (int rank = 7; rank >= 0; --rank) {
    std::size_t empty = 0;
    for (int file = 0; file < 8; ++file) {
      const Piece piece = piece_at(Square::from_file_rank(file, rank));
      if (piece.is_empty()) {
        ++empty;
      } else {
        if (empty != 0) {
          fen += static_cast<char>('0' + empty);
          empty = 0;
        }
        fen += piece_to_fen(piece);
      }
    }
    if (empty != 0) {
      fen += static_cast<char>('0' + empty);
    }
    if (rank != 0) {
      fen += '/';
    }
  }

  fen += side_to_move_ == Color::White ? " w " : " b ";
  const CastlingRights rights = castling_rights_;
  if (!rights.white_king_side && !rights.white_queen_side &&
      !rights.black_king_side && !rights.black_queen_side) {
    fen += '-';
  } else {
    if (rights.white_king_side) fen += 'K';
    if (rights.white_queen_side) fen += 'Q';
    if (rights.black_king_side) fen += 'k';
    if (rights.black_queen_side) fen += 'q';
  }
  fen += ' ';
  if (en_passant_target_.is_valid()) {
    fen += static_cast<char>('a' + en_passant_target_.file());
    fen += static_cast<char>('1' + en_passant_target_.rank());
  } else {
    fen += '-';
  }
  fen += ' ' + std::to_string(halfmove_clock_);
  fen += ' ' + std::to_string(fullmove_number_);
  return fen;
}

}  // namespace hebichess
