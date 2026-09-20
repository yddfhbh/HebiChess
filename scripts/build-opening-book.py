#!/usr/bin/env python3
"""Build the compact Phase 7-4A HebiChess opening-book binary."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

MAGIC = b"HEBIBOOK"
VERSION = 1
SEED = 0x9E3779B97F4A7C15
MASK = (1 << 64) - 1


def next_key(value: int) -> int:
    value = (value + SEED) & MASK
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK
    return (value ^ (value >> 31)) & MASK


def _keys() -> tuple[list[list[list[int]]], list[int], list[int], int]:
    value = SEED
    pieces = [[[0] * 64 for _ in range(2)] for _ in range(7)]
    for typ in range(7):
        for color in range(2):
            for square in range(64):
                pieces[typ][color][square] = next_key(value)
                value += 1
    castling = [next_key(value + i) for i in range(16)]
    value += 16
    ep = [next_key(value + i) for i in range(64)]
    value += 64
    return pieces, castling, ep, next_key(value)


PIECE_KEYS, CASTLING_KEYS, EP_KEYS, SIDE_KEY = _keys()
PIECE_TYPE = {"": 0, "p": 1, "n": 2, "b": 3, "r": 4, "q": 5, "k": 6}
PROMOTION = {"": 0, "n": 1, "b": 2, "r": 3, "q": 4}


def parse_board(fen: str) -> tuple[list[str], int, str, int | None]:
    fields = fen.split()
    if len(fields) < 4:
        raise ValueError(f"invalid FEN: {fen}")
    board: list[str] = [""] * 64
    for rank, row in enumerate(fields[0].split("/")):
        file = 0
        for symbol in row:
            if symbol.isdigit():
                file += int(symbol)
            else:
                board[(7 - rank) * 8 + file] = symbol
                file += 1
        if file != 8:
            raise ValueError(f"invalid FEN board: {fen}")
    side = 0 if fields[1] == "w" else 1 if fields[1] == "b" else -1
    if side < 0:
        raise ValueError(f"invalid FEN side: {fen}")
    ep = None if fields[3] == "-" else (ord(fields[3][0]) - 97) + 8 * (int(fields[3][1]) - 1)
    return board, side, fields[2], ep


def attacked(board: list[str], square: int, by_side: int) -> bool:
    file, rank = square % 8, square // 8
    pawn = "P" if by_side == 0 else "p"
    pawn_rank = rank - 1 if by_side == 0 else rank + 1
    for df in (-1, 1):
        f = file + df
        if 0 <= f < 8 and 0 <= pawn_rank < 8 and board[pawn_rank * 8 + f] == pawn:
            return True
    knight = "N" if by_side == 0 else "n"
    for df, dr in ((1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)):
        f, r = file + df, rank + dr
        if 0 <= f < 8 and 0 <= r < 8 and board[r * 8 + f] == knight:
            return True
    for df, dr, kinds in ((1, 0, "rq"), (-1, 0, "rq"), (0, 1, "rq"), (0, -1, "rq"),
                          (1, 1, "bq"), (1, -1, "bq"), (-1, 1, "bq"), (-1, -1, "bq")):
        f, r = file + df, rank + dr
        while 0 <= f < 8 and 0 <= r < 8:
            piece = board[r * 8 + f]
            if piece:
                if piece.isupper() == (by_side == 0) and piece.lower() in kinds:
                    return True
                break
            f, r = f + df, r + dr
    king = "K" if by_side == 0 else "k"
    return any(0 <= file + df < 8 and 0 <= rank + dr < 8 and
               board[(rank + dr) * 8 + file + df] == king
               for df in (-1, 0, 1) for dr in (-1, 0, 1) if df or dr)


def legal_ep(board: list[str], side: int, ep: int) -> bool:
    ef, er = ep % 8, ep // 8
    source_rank = er - 1 if side == 0 else er + 1
    pawn = "P" if side == 0 else "p"
    enemy_pawn = "p" if side == 0 else "P"
    if not (0 <= source_rank < 8) or board[er * 8 + ef] or board[(er - (1 if side == 0 else -1)) * 8 + ef] != enemy_pawn:
        return False
    king = "K" if side == 0 else "k"
    king_square = next((i for i, p in enumerate(board) if p == king), None)
    if king_square is None:
        return False
    for df in (-1, 1):
        sf = ef + df
        if not (0 <= sf < 8) or board[source_rank * 8 + sf] != pawn:
            continue
        trial = board.copy()
        source = source_rank * 8 + sf
        captured = (er - (1 if side == 0 else -1)) * 8 + ef
        trial[source], trial[ep], trial[captured] = "", pawn, ""
        if not attacked(trial, king_square, 1 - side):
            return True
    return False


def book_key(fen: str) -> int:
    board, side, castling, ep = parse_board(fen)
    key = SIDE_KEY if side else 0
    rights = sum(1 << i for i, flag in enumerate("KQkq") if flag in castling)
    key ^= CASTLING_KEYS[rights]
    if ep is not None and legal_ep(board, side, ep):
        key ^= EP_KEYS[ep]
    for square, piece in enumerate(board):
        if piece:
            key ^= PIECE_KEYS[PIECE_TYPE[piece.lower()]][0 if piece.isupper() else 1][square]
    return key


def pack_uci(uci: str) -> int:
    if len(uci) not in (4, 5) or not all("a" <= uci[i] <= "h" and "1" <= uci[i + 1] <= "8" for i in (0, 2)):
        raise ValueError(f"invalid UCI move: {uci}")
    promotion = PROMOTION.get(uci[4:].lower())
    if promotion is None:
        raise ValueError(f"invalid promotion: {uci}")
    source = (ord(uci[0]) - 97) + 8 * (ord(uci[1]) - 49)
    target = (ord(uci[2]) - 97) + 8 * (ord(uci[3]) - 49)
    return source | (target << 6) | (promotion << 12)


def position_records(data: dict[str, Any]) -> list[tuple[str, list[tuple[str, int]]]]:
    raw = data.get("positions", data.get("book", data.get("entries")))
    if raw is None:
        raise ValueError("input JSON has no positions")
    items = raw.items() if isinstance(raw, dict) else ((None, item) for item in raw)
    result = []
    for key, item in items:
        fen = key if isinstance(key, str) and "/" in key else item.get("fen", item.get("position"))
        if not fen:
            raise ValueError("position has no fen")
        moves = item if isinstance(item, list) else item.get("moves", item.get("candidates", []))
        candidates: list[tuple[str, int]] = []
        for move in moves:
            if isinstance(move, str):
                candidates.append((move, 1))
            else:
                uci = move.get("uci", move.get("move"))
                weight = int(move.get("weight", move.get("count", move.get("frequency", 0))))
                candidates.append((uci, weight))
        result.append((fen, candidates))
    return result


def build(input_path: Path, output_path: Path, report_path: Path) -> None:
    raw = input_path.read_bytes()
    data = json.loads(raw)
    grouped: dict[int, tuple[str, dict[str, int]]] = {}
    for fen, candidates in position_records(data):
        key = book_key(fen)
        canonical = " ".join(fen.split()[:4])
        if key in grouped and grouped[key][0] != canonical:
            raise ValueError(f"64-bit key collision: {grouped[key][0]} vs {canonical}")
        bucket = grouped.setdefault(key, (canonical, {}))[1]
        for uci, weight in candidates:
            packed = pack_uci(uci)
            if weight <= 0:
                raise ValueError(f"non-positive weight for {uci}")
            bucket[uci] = bucket.get(uci, 0) + weight
    positions = sorted(grouped.items())
    moves = []
    entries = []
    for key, (_, candidates) in positions:
        offset = len(moves)
        for uci, weight in sorted(candidates.items()):
            moves.append((pack_uci(uci), weight))
        entries.append((key, offset, len(moves) - offset))
    max_ply = int(data.get("policy", {}).get("max_book_ply", data.get("max_book_ply", 0)))
    min_count = int(data.get("policy", {}).get("min_move_count", data.get("min_move_count", 0)))
    output = bytearray(struct.pack("<8sIIIHH", MAGIC, VERSION, len(entries), len(moves), max_ply, min_count))
    for key, offset, count in entries:
        output += struct.pack("<QIHH", key, offset, count, 0)
    for packed, weight in moves:
        output += struct.pack("<HHI", packed, 0, weight)
    output_path.write_bytes(output)
    report = {"input_sha256": hashlib.sha256(raw).hexdigest(), "output_sha256": hashlib.sha256(output).hexdigest(),
              "format_version": VERSION, "positions": len(entries), "candidate_moves": len(moves),
              "total_retained_weight": sum(weight for _, weight in moves), "max_book_ply": max_ply,
              "min_move_count": min_count, "output_bytes": len(output)}
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    build(args.input, args.output, args.report or args.output.with_suffix(".report.json"))


if __name__ == "__main__":
    main()
