"""HalfKP-v1 feature ABI shared with src/chess/nnue_features.cpp."""
from __future__ import annotations

INPUT_DIM = 64 * 12 * 64
FEATURE_SET_V1 = 1

PIECE_TYPE = {"P": 0, "N": 1, "B": 2, "R": 3, "Q": 4, "K": 5}

def parse_fen(fen: str):
    fields = fen.split()
    if len(fields) < 2: raise ValueError("FEN needs placement and side to move")
    board = []
    ranks = fields[0].split("/")
    if len(ranks) != 8: raise ValueError("invalid FEN placement")
    for fen_rank, text in enumerate(ranks):
        rank, file = 7 - fen_rank, 0
        for ch in text:
            if ch.isdigit(): file += int(ch)
            elif ch.upper() in PIECE_TYPE:
                if file >= 8: raise ValueError("invalid FEN rank")
                board.append((rank * 8 + file, ch)); file += 1
            else: raise ValueError("invalid FEN piece")
        if file != 8: raise ValueError("invalid FEN rank width")
    return board, fields[1]

def active_features(fen: str, perspective: str):
    """Return physical-square ordered v1 indices for `white` or `black`."""
    board, _ = parse_fen(fen)
    own_upper = perspective.lower() == "white"
    king = next((sq for sq, p in board if p == ("K" if own_upper else "k")), None)
    if king is None: return []
    orient = lambda square: square if own_upper else square ^ 56
    king_square = orient(king)
    result = []
    for square, piece in sorted(board):
        own = piece.isupper() == own_upper
        colored_type = (0 if own else 6) + PIECE_TYPE[piece.upper()]
        result.append(((king_square * 12 + colored_type) * 64) + orient(square))
    return result

def both_features(fen: str): return active_features(fen, "white"), active_features(fen, "black")
