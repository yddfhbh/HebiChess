"""Label extracted FENs with an external Stockfish-compatible UCI engine."""
from __future__ import annotations
import argparse
from pathlib import Path

from .dataset import sample_from_fen, write


def require_chess():
    try:
        import chess, chess.engine
        return chess, chess.engine
    except ImportError as error:
        raise SystemExit("python-chess is required; install with: pip install python-chess") from error


def position_lines(path: Path):
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip(): continue
        fields = line.split("\t", 1)
        try: game_id, fen = (int(fields[0]), fields[1]) if len(fields) == 2 else (line_no, fields[0])
        except ValueError as error: raise ValueError(f"{path}:{line_no}: invalid game id") from error
        yield game_id, fen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--positions", type=Path, required=True)
    parser.add_argument("--stockfish", type=Path, required=True, help="external UCI executable; never bundled")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--nodes", type=int, default=10_000)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--hash", type=int, default=256)
    parser.add_argument("--cp-clamp", type=int, default=2000)
    parser.add_argument("--mate-policy", choices=("exclude", "clamp"), default="exclude")
    args = parser.parse_args()
    if args.nodes <= 0 or args.cp_clamp <= 0: parser.error("--nodes and --cp-clamp must be positive")
    chess, engine_api = require_chess()
    rows = list(position_lines(args.positions)); accepted = []; mates = 0
    engine = engine_api.SimpleEngine.popen_uci(str(args.stockfish))
    try:
        available = engine.options
        configuration = {"Threads": args.threads, "Hash": args.hash}
        engine.configure({name: value for name, value in configuration.items() if name in available})
        for number, (game_id, fen) in enumerate(rows, 1):
            board = chess.Board(fen)
            info = engine.analyse(board, engine_api.Limit(nodes=args.nodes))
            score = info["score"].pov(board.turn)
            mate = score.mate()
            if mate is not None:
                mates += 1
                if args.mate_policy == "exclude": continue
                target = args.cp_clamp if mate > 0 else -args.cp_clamp
                accepted.append(sample_from_fen(fen, target, mate=mate, game_id=game_id))
            else:
                target = max(-args.cp_clamp, min(args.cp_clamp, score.score()))
                accepted.append(sample_from_fen(fen, target, game_id=game_id))
            if number % 1000 == 0: print(f"labeled {number}/{len(rows)}; accepted {len(accepted)}")
    finally:
        engine.quit()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = write(args.output, accepted)
    print(f"wrote {count} samples to {args.output}; nodes={args.nodes}, cp clamp=±{args.cp_clamp}, mates={mates} ({args.mate_policy})")


if __name__ == "__main__": main()
