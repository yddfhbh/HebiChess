#!/usr/bin/env python3
"""Deterministic paired self-play for the frozen Phase 6-4B A/B binaries."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import subprocess
import threading
import time
from typing import Any


FROZEN_NETWORK_SHA256 = "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826"
BUILD_PREFIX = "info string strength_build "
STATS_PREFIX = "info string strength_stats "
OPENING_SUITE_SCHEMA = "hebichess-phase6-4b-opening-suite-v1"
OPENING_SUITE_VERSION = "phase6-4b-7c-book500-v1"
OPENING_SUITE_SHA256 = "c942fad3c5bbc259758e0c264ff6dc4650aefa94e4130377c699e2de21fb460b"
OPENING_SUITE_SIZE = 500
DEFAULT_OPENING_SUITE = Path("tests/data/strength-openings-500.json")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--current", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--network", type=Path)
    parser.add_argument("--network-sha256", default=FROZEN_NETWORK_SHA256)
    parser.add_argument("--eval-mode", choices=("NNUE", "HCE"), default="NNUE")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--games", type=int, default=400)
    parser.add_argument("--movetime-ms", type=int, default=100)
    parser.add_argument("--opening-suite", type=Path, default=DEFAULT_OPENING_SUITE,
                        help="fixed, versioned JSON opening suite (default: %(default)s)")
    parser.add_argument("--audit-openings", action="store_true",
                        help="validate and print opening-suite metadata, then exit")
    parser.add_argument("--max-game-plies", type=int, default=180)
    parser.add_argument("--win-adjudication-cp", type=int, default=900)
    parser.add_argument("--win-adjudication-plies", type=int, default=8)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.games < 2 or args.games % 2:
        parser.error("--games must be a positive even number (at least 2)")
    if args.movetime_ms < 1:
        parser.error("--movetime-ms must be positive")
    if args.eval_mode == "NNUE" and args.network is None and not args.audit_openings:
        parser.error("--network is required for NNUE")
    return args


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class EngineFailure(RuntimeError):
    def __init__(self, kind: str, message: str):
        super().__init__(message)
        self.kind = kind


class Engine:
    def __init__(self, name: str, binary: Path, eval_mode: str, network: Path | None):
        self.name = name
        self.binary = binary.resolve()
        self.eval_mode = eval_mode
        self.network = network.resolve() if network else None
        self.process: subprocess.Popen[str] | None = None
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.stderr: list[str] = []
        self.build: dict[str, bool] = {}
        self.start()

    def start(self) -> None:
        self.close()
        self.lines = queue.Queue()
        self.stderr = []
        try:
            self.process = subprocess.Popen(
                [str(self.binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
                bufsize=1)
        except OSError as error:
            raise EngineFailure("crash", f"cannot start {self.name}: {error}") from error
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()
        self.send("uci")
        handshake = self.read_until(lambda line: line == "uciok", 10.0)
        build_lines = [line for line in handshake if line.startswith(BUILD_PREFIX)]
        if len(build_lines) != 1:
            raise EngineFailure("protocol", f"{self.name} omitted unique strength_build metadata")
        tokens = build_lines[0][len(BUILD_PREFIX):].split()
        if len(tokens) % 2:
            raise EngineFailure("protocol", f"malformed strength_build from {self.name}")
        if any(tokens[i + 1] not in ("true", "false") for i in range(0, len(tokens), 2)):
            raise EngineFailure("protocol", f"invalid strength_build booleans from {self.name}")
        self.build = {tokens[i]: tokens[i + 1] == "true" for i in range(0, len(tokens), 2)}
        if self.eval_mode == "NNUE":
            assert self.network is not None
            self.send(f"setoption name EvalFile value {self.network}")
            self.read_until(lambda line: line.startswith("info string NNUE network loaded "), 30.0)
            self.send("setoption name EvalMode value NNUE")
            self.read_until(lambda line: line == "info string EvalMode NNUE", 5.0)
        else:
            self.send("setoption name EvalMode value HCE")
            self.read_until(lambda line: line == "info string EvalMode HCE", 5.0)
        self.send("isready")
        self.read_until(lambda line: line == "readyok", 5.0)

    def _read_stdout(self) -> None:
        assert self.process is not None and self.process.stdout is not None
        for line in self.process.stdout:
            self.lines.put(line.rstrip("\r\n"))
        self.lines.put(None)

    def _read_stderr(self) -> None:
        assert self.process is not None and self.process.stderr is not None
        for line in self.process.stderr:
            self.stderr.append(line.rstrip("\r\n"))

    def send(self, command: str) -> None:
        if self.process is None or self.process.poll() is not None or self.process.stdin is None:
            raise EngineFailure("crash", f"{self.name} is not running")
        try:
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as error:
            raise EngineFailure("crash", f"write to {self.name} failed: {error}") from error

    def read_until(self, predicate, timeout: float) -> list[str]:
        deadline = time.monotonic() + timeout
        result: list[str] = []
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise EngineFailure("timeout", f"{self.name} timed out; last lines={result[-5:]}")
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty as error:
                raise EngineFailure("timeout", f"{self.name} timed out; last lines={result[-5:]}") from error
            if line is None:
                code = None if self.process is None else self.process.poll()
                raise EngineFailure("crash", f"{self.name} exited ({code}); stderr={self.stderr[-5:]}")
            result.append(line)
            if predicate(line):
                return result

    def command_line(self, command: str, prefix: str, timeout: float = 5.0) -> str:
        self.send(command)
        lines = self.read_until(lambda line: line.startswith(prefix), timeout)
        return next(line for line in reversed(lines) if line.startswith(prefix))

    def new_game(self) -> None:
        self.send("ucinewgame")
        self.send("isready")
        self.read_until(lambda line: line == "readyok", 5.0)

    def position(self, opening: list[str], moves: list[str]) -> None:
        all_moves = opening + moves
        self.send("position startpos" + (" moves " + " ".join(all_moves) if all_moves else ""))

    def legal_moves(self, opening: list[str]) -> list[str]:
        self.position(opening, [])
        line = self.command_line("legalmoves", "legalmoves")
        return line.split()[1:]

    def fen(self, opening: list[str], moves: list[str]) -> str:
        self.position(opening, moves)
        return self.command_line("fen", "fen ")[4:]

    def go(self, opening: list[str], moves: list[str], movetime_ms: int) -> tuple[str, dict[str, Any]]:
        self.position(opening, moves)
        # Measurement belongs entirely to the harness.  It surrounds UCI I/O
        # only and is never sent to, nor consulted by, the engine.
        started = time.perf_counter()
        self.send(f"go movetime {movetime_ms}")
        timeout = max(5.0, movetime_ms / 1000.0 * 10.0 + 2.0)
        lines = self.read_until(lambda line: line.startswith("bestmove "), timeout)
        wall_elapsed_ms = (time.perf_counter() - started) * 1000.0
        bestmove = next(line.split()[1] for line in reversed(lines) if line.startswith("bestmove "))
        stats_lines = [line for line in lines if line.startswith(STATS_PREFIX)]
        if len(stats_lines) != 1:
            raise EngineFailure("protocol", f"{self.name} emitted {len(stats_lines)} stats lines")
        stats = parse_stats(stats_lines[0])
        stats["wall_elapsed_ms"] = wall_elapsed_ms
        return bestmove, stats

    def close(self) -> None:
        if self.process is None:
            return
        if self.process.poll() is None:
            try:
                self.send("quit")
                self.process.wait(timeout=2.0)
            except Exception:
                self.process.kill()
                self.process.wait(timeout=2.0)
        self.process = None


def parse_stats(line: str) -> dict[str, Any]:
    tokens = line[len(STATS_PREFIX):].split()
    if len(tokens) % 2:
        raise EngineFailure("protocol", f"stats are not label/value pairs: {line}")
    result: dict[str, Any] = {}
    for index in range(0, len(tokens), 2):
        label, value = tokens[index], tokens[index + 1]
        if label in result:
            raise EngineFailure("protocol", f"duplicate stats label {label}")
        try:
            result[label] = float(value) if "." in value else int(value)
        except ValueError:
            result[label] = value
    if set(result) != {"score_cp", "completed_depth"}:
        raise EngineFailure("protocol", f"unexpected strength_stats schema: {sorted(result)}")
    return result


def canonical_openings(openings: list[dict[str, Any]]) -> bytes:
    return json.dumps(openings, sort_keys=True, separators=(",", ":")).encode()


def material_balance_cp(fen: str) -> int:
    values = {"p": 100, "n": 320, "b": 330, "r": 500, "q": 900}
    balance = 0
    for piece in fen.split()[0]:
        value = values.get(piece.lower(), 0)
        balance += value if piece.isupper() else -value
    return balance


def load_opening_suite(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not path.is_file():
        raise RuntimeError(f"opening suite not found: {path}")
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema") != OPENING_SUITE_SCHEMA:
        raise RuntimeError(f"unexpected opening-suite schema: {payload.get('schema')!r}")
    version = payload.get("version")
    if version != OPENING_SUITE_VERSION:
        raise RuntimeError(f"unexpected opening-suite version: {version!r}")
    openings = payload.get("openings")
    if not isinstance(openings, list):
        raise RuntimeError("opening suite has no openings list")
    expected_hash = payload.get("openings_sha256")
    actual_hash = hashlib.sha256(canonical_openings(openings)).hexdigest()
    if expected_hash != actual_hash:
        raise RuntimeError(f"opening-suite SHA-256 mismatch: expected {expected_hash}, got {actual_hash}")
    if actual_hash != OPENING_SUITE_SHA256:
        raise RuntimeError(f"unexpected opening-suite SHA-256: {actual_hash}")

    fens: list[str] = []
    plies: list[int] = []
    material_balances: list[int] = []
    sides = {"white": 0, "black": 0}
    for expected_index, opening in enumerate(openings):
        if not isinstance(opening, dict) or opening.get("index") != expected_index:
            raise RuntimeError(f"opening {expected_index} has a non-contiguous index")
        moves, fen = opening.get("moves"), opening.get("fen")
        if (not isinstance(moves, list) or not all(isinstance(move, str) and len(move) in (4, 5)
                                                   for move in moves) or
                not isinstance(fen, str) or len(fen.split()) != 6):
            raise RuntimeError(f"opening {expected_index} has invalid moves or FEN")
        side = fen.split()[1]
        if side not in ("w", "b"):
            raise RuntimeError(f"opening {expected_index} has invalid FEN side-to-move")
        fens.append(fen)
        plies.append(len(moves))
        material_balances.append(material_balance_cp(fen))
        sides["white" if side == "w" else "black"] += 1
    duplicates = len(fens) - len(set(fens))
    if duplicates:
        raise RuntimeError(f"opening suite has {duplicates} duplicate FENs")
    if not openings:
        raise RuntimeError("opening suite is empty")
    if len(openings) != OPENING_SUITE_SIZE:
        raise RuntimeError(f"opening suite has {len(openings)} positions, expected {OPENING_SUITE_SIZE}")
    if any(balance != 0 for balance in material_balances):
        raise RuntimeError("opening suite contains a material-imbalanced position")
    if sides != {"white": 250, "black": 250}:
        raise RuntimeError(f"opening suite side-to-move imbalance: {sides}")
    if [min(plies), max(plies)] != [9, 16]:
        raise RuntimeError(f"opening suite ply range is {[min(plies), max(plies)]}, expected [9, 16]")
    metadata = {
        "schema": payload["schema"], "version": version,
        "openings_sha256": actual_hash, "total_unique_openings": len(openings),
        "duplicate_count": duplicates, "side_to_move": sides,
        "ply_range": [min(plies), max(plies)],
        "material_balance_cp_range": [min(material_balances), max(material_balances)],
        "original_200_preserved_as_prefix": payload.get("original_200_preserved_as_prefix", False),
        "original_200_prefix_note": payload.get("original_200_prefix_note", "not recorded"),
    }
    return openings, metadata


def verify_opening_suite(engine: Engine, openings: list[dict[str, Any]]) -> None:
    for opening in openings:
        observed = engine.fen(opening["moves"], [])
        if observed != opening["fen"]:
            raise RuntimeError(
                f"opening {opening['index']} replay FEN mismatch: expected {opening['fen']}, got {observed}")


def repetition_key(fen: str) -> str:
    return " ".join(fen.split()[:4])


def insufficient_material(fen: str) -> bool:
    placement = fen.split()[0]
    pieces = [ch.lower() for ch in placement if ch.isalpha() and ch.lower() != "k"]
    return not pieces or (len(pieces) == 1 and pieces[0] in ("b", "n"))


def empty_totals() -> dict[str, float]:
    return {key: 0.0 for key in ("searches", "completed_depth", "wall_elapsed_ms")}


def add_stats(total: dict[str, float], stats: dict[str, Any]) -> None:
    total["searches"] += 1
    for key in total:
        if key != "searches":
            total[key] += float(stats.get(key, 0))


def play_game(current: Engine, candidate: Engine, opening: dict[str, Any],
              candidate_color: str, args: argparse.Namespace) -> dict[str, Any]:
    for engine in (current, candidate):
        engine.new_game()
    moves: list[str] = []
    fen = opening["fen"]
    repetitions = {repetition_key(fen): 1}
    totals = {"current": empty_totals(), "candidate": empty_totals()}
    high_side: str | None = None
    high_count = 0
    result = "1/2-1/2"
    reason = "max_game_plies"
    failure_engine = None
    for ply in range(args.max_game_plies):
        side = "white" if ply % 2 == 0 else "black"
        candidate_to_move = side == candidate_color
        name = "candidate" if candidate_to_move else "current"
        engine = candidate if candidate_to_move else current
        try:
            bestmove, stats = engine.go(opening["moves"], moves, args.movetime_ms)
        except EngineFailure as error:
            result = ("0-1" if side == "white" else "1-0")
            reason = error.kind
            failure_engine = name
            break
        add_stats(totals[name], stats)
        score = int(stats.get("score_cp", 0))
        if bestmove == "0000":
            if score <= -29000:
                result = "0-1" if side == "white" else "1-0"
                reason = "checkmate"
            else:
                reason = "stalemate"
            break
        moves.append(bestmove)
        fen = current.fen(opening["moves"], moves)
        fields = fen.split()
        key = repetition_key(fen)
        repetitions[key] = repetitions.get(key, 0) + 1
        if repetitions[key] >= 3:
            reason = "threefold_repetition"
            break
        if len(fields) >= 5 and int(fields[4]) >= 100:
            reason = "fifty_move_rule"
            break
        if insufficient_material(fen):
            reason = "insufficient_material"
            break
        white_score = score if side == "white" else -score
        observed = "white" if white_score >= args.win_adjudication_cp else (
            "black" if white_score <= -args.win_adjudication_cp else None)
        if observed is not None and observed == high_side:
            high_count += 1
        else:
            high_side, high_count = observed, 1 if observed else 0
        if high_count >= args.win_adjudication_plies:
            result = "1-0" if high_side == "white" else "0-1"
            reason = "score_adjudication"
            break
    candidate_win = (result == "1-0" and candidate_color == "white") or (
        result == "0-1" and candidate_color == "black")
    candidate_loss = (result == "0-1" and candidate_color == "white") or (
        result == "1-0" and candidate_color == "black")
    outcome = "win" if candidate_win else "loss" if candidate_loss else "draw"
    return {"opening": opening["index"], "candidate_color": candidate_color,
            "result": result, "candidate_outcome": outcome, "reason": reason,
            "failure_engine": failure_engine, "plies": len(moves), "moves": moves,
            "final_fen": fen, "stats": totals}


def elo(score: float) -> float:
    bounded = min(1.0 - 1e-12, max(1e-12, score))
    return 400.0 * math.log10(bounded / (1.0 - bounded))


def summarize(games: list[dict[str, Any]]) -> dict[str, Any]:
    wins = sum(game["candidate_outcome"] == "win" for game in games)
    draws = sum(game["candidate_outcome"] == "draw" for game in games)
    losses = sum(game["candidate_outcome"] == "loss" for game in games)
    count = len(games)
    score = (wins + 0.5 * draws) / count if count else 0.5
    pairs = [games[index:index + 2] for index in range(0, count - 1, 2)]
    pair_scores = [sum(1.0 if game["candidate_outcome"] == "win" else
                       0.5 if game["candidate_outcome"] == "draw" else 0.0
                       for game in pair) / 2.0 for pair in pairs]
    if len(pair_scores) > 1:
        mean = sum(pair_scores) / len(pair_scores)
        variance = sum((value - mean) ** 2 for value in pair_scores) / (len(pair_scores) - 1)
        margin = 1.96 * math.sqrt(variance / len(pair_scores))
    else:
        margin = 0.5
    low, high = max(0.0, score - margin), min(1.0, score + margin)
    aggregate = {name: empty_totals() for name in ("current", "candidate")}
    for game in games:
        for name in aggregate:
            for key, value in game["stats"][name].items():
                aggregate[name][key] += value
    performance: dict[str, Any] = {}
    for name, values in aggregate.items():
        searches = max(1.0, values["searches"])
        elapsed_s = values["wall_elapsed_ms"] / 1000.0
        performance[name] = {
            "average_completed_depth": values["completed_depth"] / searches,
            "mean_wall_elapsed_ms": values["wall_elapsed_ms"] / searches,
            "searches_per_second": searches / elapsed_s if elapsed_s else 0.0,
        }
    return {
        "games": count, "opening_pairs": len(pairs), "wins": wins, "draws": draws,
        "losses": losses, "score_pct": score * 100.0, "elo": elo(score),
        "score_95ci_pct": [low * 100.0, high * 100.0],
        "elo_95ci": [elo(low), elo(high)],
        "confidence_method": "normal 95% CI over opening-pair mean scores",
        "timeouts": sum(game["reason"] == "timeout" for game in games),
        "crashes": sum(game["reason"] == "crash" for game in games),
        "protocol_failures": sum(game["reason"] == "protocol" for game in games),
        "performance": performance,
    }


def atomic_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def print_summary(summary: dict[str, Any]) -> None:
    print(f"W/D/L {summary['wins']}/{summary['draws']}/{summary['losses']}  "
          f"score {summary['score_pct']:.2f}%  Elo {summary['elo']:+.1f}  "
          f"95% CI [{summary['elo_95ci'][0]:+.1f}, {summary['elo_95ci'][1]:+.1f}]")
    print(f"timeouts={summary['timeouts']} crashes={summary['crashes']} "
          f"protocol_failures={summary['protocol_failures']}")
    for name, perf in summary["performance"].items():
        print(f"{name}: depth={perf['average_completed_depth']:.2f} "
              f"mean wall={perf['mean_wall_elapsed_ms']:.2f}ms "
              f"searches/s={perf['searches_per_second']:.2f}")


def main() -> int:
    args = parse_args()
    openings, opening_metadata = load_opening_suite(args.opening_suite)
    if args.audit_openings:
        print(json.dumps(opening_metadata, indent=2, sort_keys=True))
        return 0
    if args.current is None or args.candidate is None or args.output is None:
        raise RuntimeError("--current, --candidate, and --output are required unless --audit-openings is used")
    if args.games // 2 > len(openings):
        raise RuntimeError(f"{args.games} games require {args.games // 2} unique openings, "
                           f"but suite provides only {len(openings)}")
    for path in (args.current, args.candidate):
        if not path.is_file():
            raise RuntimeError(f"binary not found: {path}")
    network_sha = None
    if args.eval_mode == "NNUE":
        assert args.network is not None
        if not args.network.is_file():
            raise RuntimeError(f"network not found: {args.network}")
        network_sha = sha256_file(args.network)
        if network_sha.lower() != args.network_sha256.lower():
            raise RuntimeError(f"network SHA-256 mismatch: {network_sha}")
    config = {
        "games": args.games, "movetime_ms": args.movetime_ms,
        "opening_suite_schema": opening_metadata["schema"],
        "opening_suite_version": opening_metadata["version"],
        "opening_suite_sha256": opening_metadata["openings_sha256"],
        "max_game_plies": args.max_game_plies,
        "win_adjudication_cp": args.win_adjudication_cp,
        "win_adjudication_plies": args.win_adjudication_plies,
        "eval_mode": args.eval_mode, "network_sha256": network_sha,
    }
    current = Engine("current", args.current, args.eval_mode, args.network)
    candidate = Engine("candidate", args.candidate, args.eval_mode, args.network)
    try:
        if current.build != {"qdelta_pruning": True, "qtt": False, "qsearch_tt_profile": False}:
            raise RuntimeError(f"unexpected current build metadata: {current.build}")
        if candidate.build != {"qdelta_pruning": False, "qtt": True, "qsearch_tt_profile": False}:
            raise RuntimeError(f"unexpected candidate build metadata: {candidate.build}")
        # This is deliberately a replay check, not a legal-move generator.  A
        # suite whose recorded FEN cannot be reached by its supplied book line
        # is rejected before it can contribute any game to the sample.
        verify_opening_suite(current, openings)
        if args.resume and args.output.exists():
            report = json.loads(args.output.read_text(encoding="utf-8"))
            if report.get("config") != config:
                raise RuntimeError("resume configuration does not match existing report")
            if report.get("opening_suite") != opening_metadata:
                raise RuntimeError("resume opening suite does not match existing report")
            if report.get("openings") != openings:
                raise RuntimeError("resume opening list does not match fixed suite")
            games = report["game_records"]
        else:
            openings = openings[:args.games // 2]
            games = []
            report = {
                "schema": "hebichess-phase6-4b-strength-v1", "config": config,
                "binaries": {
                    "current": {"path": str(args.current), "sha256": sha256_file(args.current),
                                "build": current.build},
                    "candidate": {"path": str(args.candidate), "sha256": sha256_file(args.candidate),
                                  "build": candidate.build},
                },
                "opening_suite": opening_metadata,
                "openings_sha256": hashlib.sha256(canonical_openings(openings)).hexdigest(),
                "openings": openings, "game_records": games,
            }
        for game_index in range(len(games), args.games):
            opening = openings[game_index // 2]
            candidate_color = "white" if game_index % 2 == 0 else "black"
            game = play_game(current, candidate, opening, candidate_color, args)
            game["game"] = game_index + 1
            games.append(game)
            report["summary"] = summarize(games)
            atomic_json(args.output, report)
            if (game_index + 1) % 10 == 0 or game_index + 1 == args.games:
                print(f"after {game_index + 1}/{args.games}: ", end="")
                print_summary(report["summary"])
            if game["reason"] in ("timeout", "crash", "protocol"):
                failed = candidate if game["failure_engine"] == "candidate" else current
                failed.start()
        print_summary(report["summary"])
        print(f"wrote {args.output}")
        return 0
    finally:
        current.close()
        candidate.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        raise SystemExit(f"strength-test-qsearch-tt.py: {error}")
