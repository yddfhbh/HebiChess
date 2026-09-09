"""End-to-end C++/pure-Python NNUE parity test (no PyTorch required)."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import tempfile

from .make_test_network import make_network, write_network
from .reference import HEADER, active_features, load

POSITIONS = {
    "startpos": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "e4 e5 opening": "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "middlegame": "r1bq1rk1/pp2bppp/2n1pn2/2pp4/8/1PNP1N2/PBPP1PPP/R2Q1RK1 w - - 0 8",
    "sparse king+pawn endgame": "8/8/4k3/3p4/3P4/4K3/8/8 w - - 0 1",
    "Qe5 regression": "rnbqkbnr/pppp1ppp/8/4p3/4Q3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 2",
    "promotion possible": "4k3/P7/8/8/8/8/7p/4K3 w - - 0 1",
    "castled": "r3k2r/pppq1ppp/2npbn2/3Np3/3P4/2N1P3/PPP2PPP/R2Q1RK1 b kq - 4 8",
    "en-passant available": "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
    "black-to-move equivalent": "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2",
    "color-swapped/mirrored": "rnbqkb1r/pppp1ppp/5n2/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 1 2",
}
MIRROR_ORIGINAL = "rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2"


def engine_lines(engine: Path, network: Path, fen: str, command: str) -> list[str]:
    commands = f"setoption name EvalFile value {network}\nposition fen {fen}\n{command}\nquit\n"
    run = subprocess.run([str(engine)], input=commands, text=True, capture_output=True, check=True)
    return run.stdout.splitlines()


def cxx_features(engine: Path, network: Path, fen: str):
    lines = engine_lines(engine, network, fen, "features")
    values = {}
    for line in lines:
        parts = line.split()
        if parts and parts[0] in ("white", "black"):
            values[parts[0]] = [int(value) for value in parts[1:]]
    if set(values) != {"white", "black"}: raise AssertionError(f"missing C++ features: {lines}")
    return values


def cxx_score(engine: Path, network: Path, fen: str) -> tuple[int, float]:
    lines = engine_lines(engine, network, fen, "nnueeval")
    for line in lines:
        if line.startswith("nnue "):
            parts = line.split()
            return int(parts[1]), float(parts[3])
    raise AssertionError(f"missing C++ NNUE score: {lines}")


def check_rejections(engine: Path, network: Path, directory: Path) -> None:
    original = network.read_bytes()
    cases = {
        "bad magic": b"B" + original[1:],
        "bad version": original[:8] + (2).to_bytes(4, "little") + original[12:],
        "wrong dimension": original[:16] + (1).to_bytes(4, "little") + original[20:],
        "checksum mismatch": original[:-1] + bytes([original[-1] ^ 1]),
        "truncated header": original[:20],
        "truncated parameters": original[:-4],
    }
    expected = {"bad magic": "bad magic", "bad version": "unsupported format version",
                "wrong dimension": "incompatible feature/network dimensions",
                "checksum mismatch": "checksum mismatch", "truncated header": "truncated (header)",
                "truncated parameters": "truncated (parameters)"}
    for name, data in cases.items():
        path = directory / (name.replace(" ", "_") + ".hebinnue")
        path.write_bytes(data)
        lines = engine_lines(engine, path, POSITIONS["startpos"], "isready")
        if not any(expected[name] in line for line in lines):
            raise AssertionError(f"{name} accepted or wrong error: {lines}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--keep-network", type=Path,
                        help="validate this existing .hebinnue file without modifying it")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="hebichess-nnue-") as temp:
        directory = Path(temp)
        if args.keep_network:
            network_path = args.keep_network
            if not network_path.is_file():
                parser.error(f"--keep-network does not exist: {network_path}")
            network = load(network_path)
        else:
            network_path = directory / "test.hebinnue"
            network = make_network()
            write_network(network, network_path)
        check_rejections(args.engine, network_path, directory)
        # A color swap plus vertical mirror maps an original side's normalized
        # features to the other perspective exactly.  It also reverses STM,
        # so this is the ABI-level accumulator/order equivalence check.
        mirrored = POSITIONS["color-swapped/mirrored"]
        if (sorted(active_features(MIRROR_ORIGINAL, "white")) != sorted(active_features(mirrored, "black")) or
                sorted(active_features(MIRROR_ORIGINAL, "black")) != sorted(active_features(mirrored, "white"))):
            raise AssertionError("color-swapped/mirrored feature orientation mismatch")
        maximum = 0.0
        for name, fen in POSITIONS.items():
            for perspective in ("white", "black"):
                python = sorted(active_features(fen, perspective))
                cpp = sorted(cxx_features(args.engine, network_path, fen)[perspective])
                if python != cpp:
                    mismatch = next((i for i, pair in enumerate(zip(python, cpp)) if pair[0] != pair[1]), min(len(python), len(cpp)))
                    raise AssertionError(f"feature mismatch {name}/{perspective} at {mismatch}: Python={python}; C++={cpp}; FEN={fen}")
            python_score = network.evaluate(fen)
            cpp_score, cpp_raw = cxx_score(args.engine, network_path, fen)
            # The public C++ evaluator intentionally rounds the float network
            # output to side-to-move centipawns.
            difference = abs(python_score - cpp_raw)
            maximum = max(maximum, difference)
            if round(python_score) != cpp_score:
                raise AssertionError(f"rounded score mismatch {name}: Python={python_score}, C++={cpp_score}")
            print(f"{name}: Python={python_score:.8f} C++={cpp_raw:.8f} abs_diff={difference:.8f} rounded={cpp_score}")
        if maximum >= 1e-4: raise AssertionError(f"inference mismatch: max {maximum}")
        print(f"feature parity: {len(POSITIONS)} FENs x 2 perspectives: PASS")
        print("file rejection parity: 6 cases: PASS")
        print(f"inference parity maximum absolute difference: {maximum:.8f}")


if __name__ == "__main__":
    main()
