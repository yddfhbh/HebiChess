"""Write Python-reference raw NNUE scores for the frozen browser fixture.

This is intentionally a deployment/test artifact generator, not another
binary parser.  It uses the existing independent Python ``.hebinnue`` reader
and writes a small JSON file consumed by the browser and Node WASM parity
harnesses.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from .reference import load

FROZEN_NETWORK_SHA256 = "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826"
FROZEN_POSITIONS_SHA256 = "d9aec86e0f7b3ce549285247c589039ab6e9d7fd453d968a777f11ae33d451a4"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_text(path: Path) -> str:
    """Read a UTF-8 corpus with all line endings normalized to LF."""
    with path.open(encoding="utf-8", newline="") as source:
        return source.read().replace("\r\n", "\n").replace("\r", "\n")


def canonical_text_sha256(path: Path) -> str:
    """Return the SHA256 of the UTF-8, LF-normalized corpus text."""
    return hashlib.sha256(canonical_text(path).encode("utf-8")).hexdigest()


def fens(path: Path) -> list[str]:
    result = [line.strip() for line in canonical_text(path).splitlines()
              if line.strip() and not line.lstrip().startswith("#")]
    if len(result) != 100 or len(set(result)) != 100:
        raise ValueError(f"expected 100 unique FENs in {path}, got {len(result)}")
    return result


def reference_samples(network, positions: Path) -> list[dict[str, float | str]]:
    samples = []
    for fen in fens(positions):
        raw_cp = network.evaluate(fen)
        if not math.isfinite(raw_cp):
            raise SystemExit(f"non-finite Python raw NNUE score for {fen}")
        samples.append({"fen": fen, "raw_cp": raw_cp})
    return samples


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--network", type=Path, required=True)
    parser.add_argument("--positions", type=Path,
                        default=Path("tests/data/wasm-parity-100.fen"))
    parser.add_argument("--output", type=Path,
                        default=Path("tests/data/nnue-export-parity-100.json"))
    parser.add_argument("--expected-network-sha256", default=FROZEN_NETWORK_SHA256)
    parser.add_argument("--expected-positions-sha256", default=FROZEN_POSITIONS_SHA256)
    args = parser.parse_args()
    actual_sha = sha256(args.network)
    if actual_sha.lower() != args.expected_network_sha256.lower():
        raise SystemExit(f"network SHA256 mismatch: expected {args.expected_network_sha256}, got {actual_sha}")
    positions_sha = canonical_text_sha256(args.positions)
    if positions_sha.lower() != args.expected_positions_sha256.lower():
        raise SystemExit(f"positions SHA256 mismatch: expected {args.expected_positions_sha256}, got {positions_sha}")
    network = load(args.network)
    if network.format_version != 3 or network.final_hidden_activation != "relu":
        raise SystemExit("browser candidate must be HEBINNUE v3 with RELU final hidden activation")
    samples = reference_samples(network, args.positions)
    args.output.write_text(json.dumps({"network_sha256": actual_sha,
                                       "positions_sha256": positions_sha,
                                       "samples": samples}, indent=2) + "\n",
                           encoding="utf-8")
    print(f"wrote {len(samples)} Python raw-NNUE references to {args.output}")


if __name__ == "__main__":
    main()
