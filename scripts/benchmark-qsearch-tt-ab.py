#!/usr/bin/env python3
"""Isolate the first root-diverging test-only QSearch-TT cutoff.

This driver only invokes a binary built with HEBICHESS_QSEARCH_TT_DIAGNOSTIC.
It performs a QTT-off oracle run, an N=0 shadow run, exponential and binary
search of the cutoff serial, then a deterministic trace replay.  It is not a
performance benchmark and never calls the production HebiChess executable.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--network", required=True, type=Path)
    parser.add_argument("--case", required=True, dest="case_name")
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--depth", type=int, default=5)
    parser.add_argument("--variant", default="E", help="E, L, U, EL, EU, LU, or ALL")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--correctness-only", action="store_true")
    return parser.parse_args()


def canonical_variant(value: str) -> str:
    value = value.upper()
    if value == "ALL":
        return "ELU"
    if not value or any(ch not in "ELU" for ch in value) or len(set(value)) != len(value):
        raise ValueError("--variant must be E, L, U, EL, EU, LU, or ALL")
    return value


def default_fixture(case_name: str) -> Path:
    return Path("tests/data/qsearch-tt-cases") / f"{case_name}.fen"


def fixture_line(path: Path, case_name: str) -> str:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines()
             if line.strip() and not line.startswith("#")]
    if len(lines) != 1:
        raise RuntimeError(f"fixture must contain exactly one frozen FEN: {path}")
    return lines[0] if "\t" in lines[0] else f"{case_name}\t{lines[0]}"


def parse_records(path: Path) -> tuple[list[dict[str, Any]], int]:
    rows: list[dict[str, Any]] = []
    header: list[str] | None = None
    for row in csv.reader(path.read_text(encoding="utf-8").splitlines(), delimiter="\t"):
        if not row:
            continue
        if row[0].startswith("#"):
            fields = [value.removeprefix("# ") for value in row]
            if fields[0] == "case":
                header = fields
            continue
        if header is None or len(row) != len(header):
            raise RuntimeError(f"malformed diagnostic record: {row!r}")
        named = dict(zip(header, row))
        required = ("case", "bestmove", "score", "qtt_cutoff_candidates",
                    "qtt_cutoff_applied")
        missing = [field for field in required if field not in named]
        if missing:
            raise RuntimeError(f"diagnostic header is missing {missing}")
        rows.append({"case": named["case"], "bestmove": named["bestmove"],
                     "score": int(named["score"]),
                     "qtt_candidates": int(named["qtt_cutoff_candidates"]),
                     "qtt_applied": int(named["qtt_cutoff_applied"])})
    if not rows:
        raise RuntimeError("diagnostic binary emitted no records")
    return rows, max(item["qtt_candidates"] for item in rows)


def parse_trace(path: Path) -> list[dict[str, str]]:
    header: list[str] | None = None
    records: list[dict[str, str]] = []
    for row in csv.reader(path.read_text(encoding="utf-8").splitlines(), delimiter="\t"):
        if not row:
            continue
        if row[0].startswith("#"):
            header = [item.removeprefix("# ") for item in row]
            continue
        if header is None or len(row) != len(header):
            raise RuntimeError("malformed QTT trace")
        records.append(dict(zip(header, row)))
    return records


def signature(records: list[dict[str, Any]]) -> list[tuple[str, str, int]]:
    return [(item["case"], item["bestmove"], item["score"]) for item in records]


def run(binary: Path, network: Path, fixture: Path, case_name: str, depth: int,
        bounds: str, limit: int, trace_cutoff: int | None = None) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="hebichess-qtt-") as directory:
        root = Path(directory)
        prepared_fixture = root / "frozen.fen"
        prepared_fixture.write_text(fixture_line(fixture, case_name) + "\n", encoding="utf-8")
        output = root / "result.tsv"
        command = [str(binary), "--network", str(network), "--fixture", str(prepared_fixture),
                   "--only", case_name, "--random-count", "0", "--depth", str(depth),
                   "--output", str(output), "--qtt-bounds", bounds,
                   "--qtt-cutoff-limit", str(limit)]
        trace = root / "trace.tsv"
        if trace_cutoff is not None:
            command.extend(["--qtt-trace-cutoff", str(trace_cutoff), "--trace-output", str(trace)])
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
        if completed.returncode:
            raise RuntimeError(f"diagnostic binary failed ({completed.returncode}): {completed.stderr.strip()}")
        records, candidates = parse_records(output)
        return {"records": records, "signature": signature(records), "candidates": candidates,
                "trace": parse_trace(trace) if trace_cutoff is not None else [],
                "stdout": completed.stdout}


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def write_tsv(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ("case", "variant", "baseline", "failing", "largest_safe_limit",
              "first_failing_cutoff", "total_possible_cutoffs", "deterministic", "trace")
    with path.open("w", encoding="utf-8", newline="") as stream:
        stream.write("\t".join(fields) + "\n")
        stream.write("\t".join(json.dumps(report.get(field), ensure_ascii=False)
                                for field in fields) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def main() -> int:
    options = parse_args()
    variant = canonical_variant(options.variant)
    fixture = options.fixture or default_fixture(options.case_name)
    if not fixture.is_file():
        raise RuntimeError(f"fixture not found: {fixture}")
    output = options.output or Path("runs/qsearch-tt-ab") / f"{options.case_name}.{variant}.json"
    tsv = output.with_suffix(".correctness.tsv")

    baseline = run(options.binary, options.network, fixture, options.case_name,
                   options.depth, "OFF", 0)
    shadow = run(options.binary, options.network, fixture, options.case_name,
                 options.depth, variant, 0)
    if shadow["signature"] != baseline["signature"]:
        raise RuntimeError("N=0 shadow run differs from the QTT-off oracle")

    low, high = 0, 1
    candidate = run(options.binary, options.network, fixture, options.case_name,
                    options.depth, variant, high)
    while candidate["signature"] == baseline["signature"]:
        if high >= shadow["candidates"]:
            report = {"schema": "hebichess-qsearch-tt-cutoff-v1", "status": "no root divergence",
                      "case": options.case_name, "variant": variant,
                      "baseline": baseline["records"], "failing": None,
                      "largest_safe_limit": shadow["candidates"], "first_failing_cutoff": None,
                      "total_possible_cutoffs": shadow["candidates"], "deterministic": True,
                      "trace": []}
            write_json(output, report)
            write_tsv(tsv, report)
            return 0
        low, high = high, high * 2
        candidate = run(options.binary, options.network, fixture, options.case_name,
                        options.depth, variant, high)

    while high - low > 1:
        middle = (low + high) // 2
        probe = run(options.binary, options.network, fixture, options.case_name,
                    options.depth, variant, middle)
        if probe["signature"] == baseline["signature"]:
            low = middle
        else:
            high = middle

    first = run(options.binary, options.network, fixture, options.case_name,
                options.depth, variant, high, high)
    repeated = run(options.binary, options.network, fixture, options.case_name,
                   options.depth, variant, high, high)
    report = {"schema": "hebichess-qsearch-tt-cutoff-v1", "status": "root divergence",
              "case": options.case_name, "variant": variant,
              "baseline": baseline["records"], "failing": first["records"],
              "largest_safe_limit": low, "first_failing_cutoff": high,
              "total_possible_cutoffs": shadow["candidates"],
              "deterministic": first["signature"] == repeated["signature"] and
              first["trace"] == repeated["trace"], "trace": first["trace"]}
    # Required order: artifacts exist and are durable before a correctness
    # mismatch is surfaced to the caller/CI job.
    write_json(output, report)
    write_tsv(tsv, report)
    if options.correctness_only:
        raise RuntimeError(f"QTT correctness mismatch; artifacts saved to {output} and {tsv}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        raise SystemExit(f"benchmark-qsearch-tt-ab.py: {error}")
