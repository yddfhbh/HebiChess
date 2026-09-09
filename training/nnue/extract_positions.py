"""Extract deduplicated, phase-balanced legal positions from PGN games."""
from __future__ import annotations
import argparse
from pathlib import Path
import random


def require_chess():
    try:
        import chess.pgn
        return chess.pgn
    except ImportError as error:
        raise SystemExit("python-chess is required; install with: pip install python-chess") from error


def canonical_fen(board) -> str:
    """Drop clocks only; placement, STM, castling and en-passant remain meaningful."""
    fields = board.fen(en_passant="fen").split()
    return " ".join(fields[:4])


def phase(board, ply: int) -> str:
    if ply <= 20: return "opening"
    material = sum(1 for piece in board.piece_map().values() if piece.piece_type != 6)
    return "endgame" if material <= 12 else "middlegame"


def reservoir_add(reservoir, item, limit, seen_count, rng):
    if len(reservoir) < limit: reservoir.append(item); return
    replace = rng.randrange(seen_count)
    if replace < limit: reservoir[replace] = item


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pgn", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--seed", type=int, default=20260909)
    args = parser.parse_args()
    if args.count <= 0: parser.error("--count must be positive")
    chess_pgn = require_chess(); rng = random.Random(args.seed)
    # Intended 20/60/20 distribution.  Shortfalls are filled from all phases.
    quotas = {"opening": args.count // 5, "middlegame": args.count * 3 // 5,
              "endgame": args.count - args.count // 5 - args.count * 3 // 5}
    pools = {name: [] for name in quotas}; seen_by_phase = {name: 0 for name in quotas}
    all_pool, all_seen, seen_fens, games = [], 0, set(), 0
    with args.pgn.open(encoding="utf-8", errors="replace") as source:
        while game := chess_pgn.read_game(source):
            board = game.board(); games += 1
            for ply, move in enumerate(game.mainline_moves(), start=1):
                if not board.is_legal(move): break
                board.push(move)
                if ply <= 4 or board.is_game_over(claim_draw=False): continue
                fen = canonical_fen(board)
                if fen in seen_fens: continue
                seen_fens.add(fen)
                kind = phase(board, ply); item = (games, fen, kind)
                seen_by_phase[kind] += 1
                reservoir_add(pools[kind], item, quotas[kind], seen_by_phase[kind], rng)
                all_seen += 1; reservoir_add(all_pool, item, args.count, all_seen, rng)
    chosen = []
    for name in ("opening", "middlegame", "endgame"): chosen.extend(pools[name])
    if len(chosen) < args.count:
        used = {fen for _, fen, _ in chosen}
        chosen.extend(item for item in all_pool if item[1] not in used)
    chosen = chosen[:args.count]; rng.shuffle(chosen)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        for game_id, fen, _ in chosen: output.write(f"{game_id}\t{fen}\n")
    counts = {name: sum(1 for _, _, kind in chosen if kind == name) for name in quotas}
    print(f"extracted {len(chosen)} unique positions from {games} games: " +
          ", ".join(f"{name}={counts[name]}" for name in counts))


if __name__ == "__main__": main()
