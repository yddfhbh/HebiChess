"""Create a deterministic, validation-only .hebinnue v1 network."""
from __future__ import annotations

from array import array
import argparse
from pathlib import Path
import random
import struct

from .reference import (ACCUMULATOR, FEATURE_SET_V1, HEADER, HIDDEN1, HIDDEN2,
                        INPUT_DIM, Network, PARAMETER_COUNT, f32, fnv1a)


def _values(rng: random.Random, count: int, limit: float) -> array:
    # Generate in Python, then explicitly round-trip each value through f32.
    return array("f", (f32(rng.uniform(-limit, limit)) for _ in range(count)))


def _biased_values(rng: random.Random, count: int, center: float, spread: float) -> array:
    return array("f", (f32(center + rng.uniform(-spread, spread)) for _ in range(count)))


def make_network(seed: int = 20260909) -> Network:
    rng = random.Random(seed)
    # Transform sums roughly 32 active rows; following layers need smaller
    # ranges so their clamp stages retain useful non-zero variation.
    parameters = array("f")
    parameters.extend(_values(rng, INPUT_DIM * ACCUMULATOR, 0.001))
    # Positive biases make the deliberately small transform visible to later
    # layers without saturating the clipped activations.
    parameters.extend(_biased_values(rng, ACCUMULATOR, 0.25, 0.01))
    parameters.extend(_values(rng, HIDDEN1 * 2 * ACCUMULATOR, 0.02))
    parameters.extend(_biased_values(rng, HIDDEN1, 0.05, 0.01))
    parameters.extend(_values(rng, HIDDEN2 * HIDDEN1, 0.04))
    parameters.extend(_biased_values(rng, HIDDEN2, 0.20, 0.01))
    # The last layer has no clamp.  A larger scale produces observable
    # centipawn outputs while the preceding activation statistics stay gentle.
    parameters.extend(_values(rng, HIDDEN2, 100.0))
    parameters.append(f32(rng.uniform(-10.0, 10.0)))
    return Network(parameters)


def write_network(network: Network, path: Path) -> None:
    payload = network.parameters.tobytes()
    if struct.pack("=I", 1) != struct.pack("<I", 1):
        native = array("f", network.parameters); native.byteswap(); payload = native.tobytes()
    header = HEADER.pack(b"HEBINNUE", 1, FEATURE_SET_V1, INPUT_DIM, ACCUMULATOR,
                         HIDDEN1, HIDDEN2, 1, 0x01020304, PARAMETER_COUNT,
                         fnv1a(payload))
    path.write_bytes(header + payload)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, default=20260909)
    args = parser.parse_args()
    write_network(make_network(args.seed), args.output)
