#!/usr/bin/env python3
"""Build the fixed Phase 6-4B 500-position opening suite.

The suite is built once from the curated opening roots and quiet book-move
vocabulary below, then checked into ``tests/data``.  It never samples from an
engine's complete legal-move list: UCI is used solely to reject a curated move
that is illegal in the current book position and to record the canonical FEN.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from typing import Iterable


SCHEMA = "hebichess-phase6-4b-opening-suite-v1"
VERSION = "phase6-4b-7c-book500-v1"

# Each root is a conventional, non-terminal opening position after four moves.
# A few roots contain only immediately recaptured, equal exchanges.  The
# continuations add only normal developing, castling, or quiet pawn moves, and
# the builder rejects any output that is not at exact material equality.
ROOTS = (
    ("e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"),
    ("e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3", "g8f6"),
    ("e2e4", "e7e5", "g1f3", "b8c6", "d2d4", "d7d6", "f1b5", "c8d7"),
    ("e2e4", "e7e5", "g1f3", "b8c6", "b1c3", "g8f6", "f1b5", "f8b4"),
    ("e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6"),
    ("e2e4", "c7c5", "g1f3", "b8c6", "d2d4", "c5d4", "f3d4", "g8f6"),
    ("e2e4", "c7c5", "g1f3", "e7e6", "d2d4", "c5d4", "f3d4", "g8f6"),
    ("e2e4", "c7c5", "g1f3", "g8f6", "e4e5", "f6d5", "b1c3", "d5b6"),
    ("e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "c1g5", "f8b4"),
    ("e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "e4e5", "f6d7"),
    ("e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "d5e4", "c3e4", "c8f5"),
    ("e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "g8f6", "e4e5", "f6d7"),
    ("e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5d8", "d2d4", "g8f6"),
    ("e2e4", "g8f6", "e4e5", "f6d5", "d2d4", "d7d6", "g1f3", "c8g4"),
    ("e2e4", "d7d6", "d2d4", "g8f6", "b1c3", "g7g6", "c1e3", "f8g7"),
    ("d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "g1f3", "f8e7"),
    ("d2d4", "d7d5", "c2c4", "c7c6", "g1f3", "g8f6", "b1c3", "e7e6"),
    ("d2d4", "g8f6", "c2c4", "e7e6", "g1f3", "d7d5", "b1c3", "f8b4"),
    ("d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "d7d5", "c4d5", "f6d5"),
    ("d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "f8g7", "e2e4", "d7d6"),
    ("d2d4", "g8f6", "g1f3", "e7e6", "c2c4", "b7b6", "g2g3", "c8b7"),
    ("c2c4", "e7e5", "b1c3", "g8f6", "g1f3", "b8c6", "g2g3", "d7d5"),
    ("c2c4", "c7c5", "b1c3", "b8c6", "g1f3", "g8f6", "g2g3", "g7g6"),
    ("g1f3", "d7d5", "g2g3", "g8f6", "f1g2", "g7g6", "e1g1", "f8g7"),
    ("g2g3", "d7d5", "f1g2", "g8f6", "d2d3", "e7e5", "g1f3", "b8c6"),
)

# Explicit book vocabulary; selection is deterministic by suite index.  The
# order is deliberately broad, keeping the resulting lines in normal opening
# territory without a PRNG or a legal-move walk.
WHITE_BOOK = (
    "e1g1", "g1f3", "b1c3", "f1c4", "f1b5", "f1d3", "c1f4", "c1g5", "c1e3",
    "c1d2", "e2e3", "d2d3", "c2c3", "g2g3", "b2b3", "h2h3", "a2a3", "d1e2",
    "d1c2", "a1b1", "f1e1", "h1e1", "b1d2", "f3d2",
)
BLACK_BOOK = (
    "e8g8", "g8f6", "b8c6", "f8c5", "f8b4", "f8d6", "c8f5", "c8g4", "c8e6",
    "c8d7", "e7e6", "d7d6", "c7c6", "g7g6", "b7b6", "h7h6", "a7a6", "d8e7",
    "d8c7", "a8b8", "f8e8", "h8e8", "b8d7", "f6d7",
)


class Uci:
    def __init__(self, binary: Path):
        self.process = subprocess.Popen([str(binary.resolve())], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, text=True, bufsize=1)
        self.send("uci")
        self.until("uciok")

    def send(self, line: str) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def until(self, expected: str) -> list[str]:
        assert self.process.stdout is not None
        lines: list[str] = []
        while True:
            line = self.process.stdout.readline().rstrip("\r\n")
            if not line:
                raise RuntimeError(f"engine ended while waiting for {expected}: {lines[-5:]}")
            lines.append(line)
            if line == expected:
                return lines

    def command(self, line: str, prefix: str) -> str:
        self.send(line)
        assert self.process.stdout is not None
        while True:
            result = self.process.stdout.readline().rstrip("\r\n")
            if not result:
                raise RuntimeError(f"engine ended while waiting for {prefix}")
            if result.startswith(prefix):
                return result

    def legal(self, moves: Iterable[str]) -> set[str]:
        moves = list(moves)
        self.send("position startpos moves " + " ".join(moves))
        return set(self.command("legalmoves", "legalmoves").split()[1:])

    def fen(self, moves: Iterable[str]) -> str:
        moves = list(moves)
        self.send("position startpos moves " + " ".join(moves))
        return self.command("fen", "fen ")[4:]

    def close(self) -> None:
        self.send("quit")
        self.process.wait(timeout=3)


def first_legal_book_move(engine: Uci, moves: list[str], book: tuple[str, ...], offset: int) -> str:
    legal = engine.legal(moves)
    for index in range(len(book)):
        move = book[(offset + index) % len(book)]
        if move in legal:
            return move
    raise RuntimeError(f"no curated continuation from {' '.join(moves)}")


def material_balance_cp(fen: str) -> int:
    values = {"p": 100, "n": 320, "b": 330, "r": 500, "q": 900}
    return sum((value if piece.isupper() else -value)
               for piece in fen.split()[0] for value in (values.get(piece.lower(), 0),))


def build(engine: Uci) -> list[dict[str, object]]:
    openings: list[dict[str, object]] = []
    seen: set[str] = set()
    # 25 roots x 20 deterministic, book-only variants = 500.  Targets cycle
    # 9..16 plies; their parity is deliberately alternated to balance side to
    # move exactly 250/250 in the final corpus.
    for family, root in enumerate(ROOTS):
        for variant in range(20):
            # At least one book continuation distinguishes every variant of a
            # root.  Retry offsets enumerate more entries from the same fixed
            # vocabulary; they never inspect or sample arbitrary legal moves.
            target = 9 + ((family * 3 + variant * 5) % 8)
            for retry in range(100):
                moves = list(root)
                while len(moves) < target:
                    book = WHITE_BOOK if len(moves) % 2 == 0 else BLACK_BOOK
                    moves.append(first_legal_book_move(
                        engine, moves, book,
                        family * 7 + variant * 11 + len(moves) * 3 + retry * 13))
                fen = engine.fen(moves)
                if material_balance_cp(fen) != 0:
                    raise RuntimeError(f"material imbalance at family={family} variant={variant}")
                if fen not in seen:
                    break
            else:
                raise RuntimeError(f"could not find unique book line at family={family} variant={variant}")
            seen.add(fen)
            openings.append({"index": len(openings), "moves": moves, "fen": fen})
    if len(openings) != 500:
        raise RuntimeError(f"expected 500 openings, got {len(openings)}")
    sides = [opening["fen"].split()[1] for opening in openings]
    if sides.count("w") != 250 or sides.count("b") != 250:
        raise RuntimeError(f"side-to-move imbalance: w={sides.count('w')} b={sides.count('b')}")
    return openings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("tests/data/strength-openings-500.json"))
    args = parser.parse_args()
    engine = Uci(args.engine)
    try:
        openings = build(engine)
    finally:
        engine.close()
    canonical = json.dumps(openings, sort_keys=True, separators=(",", ":")).encode()
    payload = {
        "schema": SCHEMA, "version": VERSION,
        "description": "Fixed curated book-opening suite for the Phase 6-4B-7c 1000-game screen.",
        "generation": "Deterministic enumeration of curated opening roots and quiet book continuations; no random legal-move walks.",
        "original_200_preserved_as_prefix": False,
        "original_200_prefix_note": "The prior 400-game report and its generated opening list were not retained in this worktree.",
        "openings_sha256": hashlib.sha256(canonical).hexdigest(),
        "openings": openings,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.output}: {len(openings)} unique openings, sha256={payload['openings_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
