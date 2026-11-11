#include "chess/game_state.hpp"

#include <algorithm>
#include <cctype>

#include "chess/movegen.hpp"
#include "chess/uci.hpp"

namespace hebichess {
namespace {

std::string repetition_key(const Board& board) {
  const std::string fen = board.to_fen();
  const auto first = fen.find(' '), second = fen.find(' ', first + 1);
  const auto third = fen.find(' ', second + 1);
  return third == std::string::npos ? fen : fen.substr(0, third);
}

char piece_letter(PieceType type) {
  switch (type) {
    case PieceType::Knight: return 'N';
    case PieceType::Bishop: return 'B';
    case PieceType::Rook: return 'R';
    case PieceType::Queen: return 'Q';
    case PieceType::King: return 'K';
    default: return 0;
  }
}

std::string square_name(Square square) {
  if (!square.is_valid()) return {};
  return {static_cast<char>('a' + square.file()), static_cast<char>('1' + square.rank())};
}

bool insufficient(const Board& board) {
  std::vector<std::pair<Piece, Square>> pieces;
  for (std::uint8_t i = 0; i < Square::kSquareCount; ++i) {
    const Square square = Square::from_index(i);
    const Piece piece = board.piece_at(square);
    if (!piece.is_empty() && piece.type != PieceType::King) pieces.push_back({piece, square});
  }
  if (pieces.empty()) return true;
  if (pieces.size() == 1 && (pieces[0].first.type == PieceType::Bishop ||
                             pieces[0].first.type == PieceType::Knight)) return true;
  if (pieces.size() == 2 && pieces[0].first.type == PieceType::Bishop &&
      pieces[1].first.type == PieceType::Bishop && pieces[0].first.color != pieces[1].first.color) {
    return ((pieces[0].second.file() + pieces[0].second.rank()) % 2) ==
           ((pieces[1].second.file() + pieces[1].second.rank()) % 2);
  }
  return false;
}

}  // namespace

std::string san_for_move(Board& board, const Move& move) {
  const Color side = board.side_to_move();
  const Piece piece = board.piece_at(move.from);
  const bool capture = !board.piece_at(move.to).is_empty() || move.flag == MoveFlag::EnPassant;
  if (move.flag == MoveFlag::CastleKingSide || move.flag == MoveFlag::CastleQueenSide) {
    const std::string castle = move.flag == MoveFlag::CastleKingSide ? "O-O" : "O-O-O";
    const UndoState undo = board.make_move(move);
    const auto replies = generate_legal_moves(board);
    const bool check = board.is_square_attacked(board.find_king(board.side_to_move()), side);
    board.unmake_move(move, undo);
    return castle + (check ? (replies.empty() ? "#" : "+") : "");
  }
  std::string san;
  if (piece.type == PieceType::Pawn) {
    if (capture) san += static_cast<char>('a' + move.from.file());
  } else {
    san += piece_letter(piece.type);
    Board copy = board;
    const auto candidates = generate_legal_moves(copy);
    bool same_file = false, same_rank = false;
    for (const Move& candidate : candidates) {
      if (candidate.to == move.to && candidate.from != move.from &&
          board.piece_at(candidate.from).type == piece.type) {
        same_file |= candidate.from.file() == move.from.file();
        same_rank |= candidate.from.rank() == move.from.rank();
      }
    }
    if (same_file) san += static_cast<char>('1' + move.from.rank());
    else if (same_rank) san += static_cast<char>('a' + move.from.file());
    else if (same_file || same_rank) san += square_name(move.from);
  }
  if (capture) san += 'x';
  san += square_name(move.to);
  if (move.is_promotion()) san += std::string("=") + piece_letter(move.promotion);
  const UndoState undo = board.make_move(move);
  const Color next = board.side_to_move();
  const bool check = board.is_square_attacked(board.find_king(next), opposite(next));
  const auto replies = generate_legal_moves(board);
  board.unmake_move(move, undo);
  if (check) san += replies.empty() ? "#" : "+";
  return san;
}

GameStateStatus terminal_status(Board& board, const std::unordered_map<std::string, unsigned>& repetitions) {
  GameStateStatus result;
  const Color side = board.side_to_move();
  const Square king = board.find_king(side);
  if (king.is_valid() && board.is_square_attacked(king, opposite(side))) result.check_square = king;
  const auto moves = generate_legal_moves(board);
  if (moves.empty()) {
    if (king.is_valid() && board.is_square_attacked(king, opposite(side))) {
      result.status = "checkmate";
      result.result = side == Color::White ? "0-1" : "1-0";
    } else { result.status = "stalemate"; result.result = "1/2-1/2"; }
  } else if (repetitions.count(repetition_key(board)) && repetitions.at(repetition_key(board)) >= 3) {
    result.status = "threefold repetition"; result.result = "1/2-1/2";
  } else if (board.halfmove_clock() >= 100) {
    result.status = "50-move rule"; result.result = "1/2-1/2";
  } else if (insufficient(board)) {
    result.status = "insufficient material"; result.result = "1/2-1/2";
  }
  return result;
}

GameState::GameState() { reset(); }
void GameState::reset() { board_ = Board::initial(); repetitions_.clear(); repetitions_[board_.to_fen()] = 1; }
bool GameState::load_fen(const std::string& fen) {
  const auto parsed = Board::from_fen(fen);
  if (!parsed) return false;
  board_ = *parsed; repetitions_.clear(); repetitions_[board_.to_fen()] = 1; return true;
}
std::string GameState::position_key() const { return repetition_key(board_); }
std::vector<std::string> GameState::legal_moves() const {
  Board copy = board_; std::vector<std::string> result;
  for (const Move& move : generate_legal_moves(copy)) result.push_back(move_to_uci(move));
  return result;
}
bool GameState::apply_uci(const std::string& uci, std::string& san, GameStateStatus& status) {
  const auto move = parse_uci_move(board_, uci);
  if (!move) return false;
  san = san_for_move(board_, *move);
  board_.make_move(*move); ++repetitions_[position_key()];
  status = this->status(); return true;
}
GameStateStatus GameState::status() const { Board copy = board_; return terminal_status(copy, repetitions_); }

}  // namespace hebichess
