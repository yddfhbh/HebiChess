#!/usr/bin/env python3
"""Frozen-network regressions for the two proven delta-pruning failures."""

from __future__ import annotations

import argparse
import csv
import hashlib
from pathlib import Path
import subprocess
import tempfile


NETWORK_SHA256 = "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826"
CASES = {
    "random-162-local": {
        "fixture": "tests/data/qsearch-no-delta-regressions/random-162-local.fen",
        "windows": (("store", -30000, 250, 250), ("hit", 249, 250, 250),
                    ("full", -30000, 30000, 461)),
    },
    "random-131-local": {
        "fixture": "tests/data/qsearch-no-delta-regressions/random-131-local.fen",
        "windows": (("store", -319, 111, -181), ("hit", -216, -146, -181),
                    ("full", -30000, 30000, -181)),
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def root_scores(path: Path) -> dict[str, int]:
    scores: dict[str, int] = {}
    header: list[str] | None = None
    with path.open(encoding="utf-8", newline="") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if not row:
                continue
            if row[0].startswith("#"):
                header = [field.removeprefix("# ") for field in row]
                continue
            if header is None or len(row) != len(header):
                raise RuntimeError(f"malformed qsearch trace: {path}")
            record = dict(zip(header, row))
            if record["record"] == "node" and record["sequence"] == "0":
                scores[record["window"]] = int(record["returned_score"])
    return scores


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--network", required=True, type=Path)
    parser.add_argument("--output-dir", type=Path)
    options = parser.parse_args()

    actual_hash = sha256(options.network)
    if actual_hash != NETWORK_SHA256:
        raise RuntimeError(f"frozen network SHA256 mismatch: {actual_hash}")

    with tempfile.TemporaryDirectory(prefix="hebichess-no-delta-regression-") as temporary:
        output_dir = options.output_dir or Path(temporary)
        output_dir.mkdir(parents=True, exist_ok=True)
        for name, case in CASES.items():
            trace = output_dir / f"{name}.tsv"
            command = [str(options.binary), "--network", str(options.network),
                       "--fixture", case["fixture"], "--only", name,
                       "--random-count", "0", "--qsearch-window-trace-output", str(trace)]
            expected: dict[str, int] = {}
            for window, alpha, beta, score in case["windows"]:
                command.extend(["--qsearch-window", window, str(alpha), str(beta), "0"])
                expected[window] = score
            completed = subprocess.run(command, text=True, capture_output=True, check=False)
            if completed.returncode:
                raise RuntimeError(f"{name} failed: {completed.stderr.strip()}")
            actual = root_scores(trace)
            if actual != expected:
                raise RuntimeError(f"{name}: expected {expected}, got {actual}")
            print(f"PASS {name}: {actual}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        raise SystemExit(f"test-qsearch-no-delta-windows.py: {error}")
