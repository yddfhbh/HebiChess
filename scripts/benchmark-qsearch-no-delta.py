#!/usr/bin/env python3
"""Run Phase 6-4B-6 frozen correctness and release-style performance A/B."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
from typing import Any


NETWORK_SHA256 = "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826"
FROZEN_FAILURES = ("random-13", "random-131", "random-137", "random-162")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(command: list[str]) -> str:
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    if completed.returncode:
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(command)}\n"
                           f"{completed.stderr.strip()}")
    return completed.stdout


def read_profile(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    header: list[str] | None = None
    with path.open(encoding="utf-8", newline="") as stream:
        for row in csv.reader(stream, delimiter="\t"):
            if not row:
                continue
            if row[0].startswith("#"):
                fields = [value.removeprefix("# ") for value in row]
                if fields[0] == "case":
                    header = fields
                continue
            if header is None or len(row) != len(header):
                raise RuntimeError(f"malformed profile row in {path}: {row!r}")
            named = dict(zip(header, row))
            required = ("case", "bestmove", "score", "nodes", "qnodes", "fen",
                        "eval_calls", "qdelta_prunes")
            missing = [field for field in required if field not in named]
            if missing:
                raise RuntimeError(f"profile header in {path} is missing {missing}")
            records.append({"case": named["case"], "bestmove": named["bestmove"],
                            "score": int(named["score"]), "nodes": int(named["nodes"]),
                            "qnodes": int(named["qnodes"]), "fen": named["fen"],
                            "eval_calls": int(named["eval_calls"]),
                            "qdelta_prunes": int(named["qdelta_prunes"])})
    return records


def profile(binary: Path, network: Path, output: Path) -> list[dict[str, Any]]:
    run([str(binary), "--network", str(network), "--output", str(output)])
    return read_profile(output)


def signatures(records: list[dict[str, Any]]) -> dict[str, tuple[str, int]]:
    return {record["case"]: (record["bestmove"], record["score"]) for record in records}


def parse_sample(output: str) -> dict[str, float | int]:
    line = next((line for line in output.splitlines() if line.startswith("median ")), None)
    if line is None:
        raise RuntimeError(f"benchmark emitted no median record: {output!r}")
    parsed: dict[str, float | int] = {}
    for field in line.split()[1:]:
        key, value = field.split("=", 1)
        parsed[key] = float(value) if "." in value else int(value)
    return parsed


def performance_sample(binary: Path, network: Path) -> dict[str, float | int]:
    return parse_sample(run([str(binary), "--network", str(network), "--rounds", "1"]))


def median_sample(samples: list[dict[str, float | int]]) -> dict[str, float | int]:
    result: dict[str, float | int] = {}
    for key in samples[0]:
        values = [float(sample[key]) for sample in samples]
        value = statistics.median(values)
        result[key] = value if any(isinstance(sample[key], float) for sample in samples) else int(value)
    return result


def percent_delta(candidate: float, baseline: float) -> float:
    return 100.0 * (candidate - baseline) / baseline if baseline else 0.0


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--network", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build-qsearch-tt"))
    parser.add_argument("--output", type=Path, default=Path("runs/no-delta-ab.json"))
    parser.add_argument("--rounds", type=int, default=10)
    parser.add_argument("--correctness-only", action="store_true")
    options = parser.parse_args()
    if options.rounds < 10:
        raise RuntimeError("--rounds must be at least 10 for the release protocol")
    actual_hash = file_sha256(options.network)
    if actual_hash != NETWORK_SHA256:
        raise RuntimeError(f"frozen network SHA256 mismatch: {actual_hash}")

    build = options.build_dir
    suffix = ".exe" if platform.system() == "Windows" else ""
    binaries = {
        "current": build / f"HebiChessSearchQttBaselineProfile{suffix}",
        "no_delta_qtt_off": build / f"HebiChessSearchNoDeltaQttOffProfile{suffix}",
        "no_delta_qtt_on": build / f"HebiChessSearchNoDeltaQttOnProfile{suffix}",
    }
    timing = {
        "current": build / f"HebiChessSearchQttBaseline{suffix}",
        "no_delta_qtt_off": build / f"HebiChessSearchNoDeltaQttOffBenchmark{suffix}",
        "no_delta_qtt_on": build / f"HebiChessSearchNoDeltaQttOnBenchmark{suffix}",
    }
    counters = {
        "current": build / f"HebiChessSearchQttBaselineCounters{suffix}",
        "no_delta_qtt_off": build / f"HebiChessSearchNoDeltaQttOffCounters{suffix}",
        "no_delta_qtt_on": build / f"HebiChessSearchNoDeltaQttOnCounters{suffix}",
    }
    performance_binaries = () if options.correctness_only else (*timing.values(), *counters.values())
    missing = [str(path) for path in (*binaries.values(), *performance_binaries)
               if not path.is_file()]
    if missing:
        raise RuntimeError("missing binaries: " + ", ".join(missing))

    artifact_dir = options.output.parent / (options.output.stem + "-profiles")
    artifact_dir.mkdir(parents=True, exist_ok=True)
    records = {name: profile(binary, options.network, artifact_dir / f"{name}.tsv")
               for name, binary in binaries.items()}
    if any(len(value) != 270 for value in records.values()):
        raise RuntimeError("correctness corpus must contain exactly 270 positions")
    off_signature = signatures(records["no_delta_qtt_off"])
    on_signature = signatures(records["no_delta_qtt_on"])
    mismatches = [{"case": name, "qtt_off": off_signature.get(name), "qtt_on": on_signature.get(name)}
                  for name in sorted(set(off_signature) | set(on_signature))
                  if off_signature.get(name) != on_signature.get(name)]
    indexed = {variant: {record["case"]: record for record in values}
               for variant, values in records.items()}
    report: dict[str, Any] = {
        "schema": "hebichess-phase6-4b-6-v1",
        "platform": platform.platform(),
        "network_sha256": actual_hash,
        "positions": 270,
        "frozen_failures": {case: {variant: indexed[variant][case] for variant in indexed}
                            for case in FROZEN_FAILURES},
        "no_delta_qtt_off_on_equal": not mismatches,
        "mismatches": mismatches,
    }
    write_json(options.output, report)
    if mismatches:
        raise RuntimeError(f"QTT OFF/ON correctness mismatch; report saved to {options.output}")
    if options.correctness_only:
        print(f"PASS: 270/270 bestmove+score equality; report={options.output}")
        return 0

    samples: dict[str, list[dict[str, float | int]]] = {name: [] for name in timing}
    forward = tuple(timing)
    for round_index in range(options.rounds):
        order = forward if round_index % 2 == 0 else tuple(reversed(forward))
        for name in order:
            samples[name].append(performance_sample(timing[name], options.network))
    medians = {name: median_sample(value) for name, value in samples.items()}
    counter_samples = {name: performance_sample(binary, options.network)
                       for name, binary in counters.items()}
    current = medians["current"]
    no_delta = medians["no_delta_qtt_off"]
    report["performance"] = {"rounds": options.rounds, "medians": medians,
        "counter_companions": counter_samples,
        "no_delta_qtt_off_vs_current_pct": {
            key: percent_delta(float(no_delta[key]), float(current[key]))
            for key in ("elapsed_ms", "nodes", "qnodes", "nps")},
        "no_delta_qtt_off_vs_current_eval_calls_pct": percent_delta(
            float(counter_samples["no_delta_qtt_off"]["eval_calls"]),
            float(counter_samples["current"]["eval_calls"]))}
    write_json(options.output, report)
    print(f"PASS: 270/270 equality and {options.rounds}-round performance A/B; report={options.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        raise SystemExit(f"benchmark-qsearch-no-delta.py: {error}")
