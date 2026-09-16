"""Binary-format constants shared by the NNUE exporter and Python reader.

All multi-byte values are little-endian.  v1 and v2 are immutable legacy
formats.  v3 adds an explicit final-hidden activation without changing the
payload tensor order or its raw float32 representation.
"""
from __future__ import annotations

import struct

MAGIC = b"HEBINNUE"
SCALAR_FLOAT32 = 1
ENDIAN_MARKER = 0x01020304

# v1: magic, 8 uint32 fields, parameter count, FNV-1a payload checksum.
V1_HEADER = struct.Struct("<8s8I2Q")
# v2: magic; format, feature ABI and five dimensions; scalar/endian; output
# scale; parameter count; FNV-1a checksum.  This is exactly 64 bytes.
V2_HEADER = struct.Struct("<8s9If2Q")
# v3: v2 plus an explicit final-hidden activation enum.  This is 68 bytes.
V3_HEADER = struct.Struct("<8s10If2Q")

CLIPPED_RELU_0_1 = 1
RELU = 2
FINAL_HIDDEN_ACTIVATIONS = {
    CLIPPED_RELU_0_1: "clipped_relu",
    RELU: "relu",
}
FINAL_HIDDEN_ACTIVATION_CODES = {
    name: code for code, name in FINAL_HIDDEN_ACTIVATIONS.items()
}


def final_hidden_activation_name(code: int) -> str:
    """Decode a v3 activation enum, failing closed for unknown values."""
    try:
        return FINAL_HIDDEN_ACTIVATIONS[code]
    except KeyError as error:
        raise ValueError("NNUE unsupported final hidden activation") from error


def final_hidden_activation_code(name: str) -> int:
    """Encode an exporter activation name, failing closed for unknown names."""
    try:
        return FINAL_HIDDEN_ACTIVATION_CODES[name]
    except KeyError as error:
        raise ValueError("unsupported final_hidden_activation metadata") from error


def parameter_count(input_dim: int, accumulator_dim: int, hidden1_dim: int,
                    hidden2_dim: int, output_dim: int) -> int:
    """Number of float32 payload values in row-major PyTorch tensor order."""
    return (input_dim * accumulator_dim + accumulator_dim +
            hidden1_dim * (2 * accumulator_dim) + hidden1_dim +
            hidden2_dim * hidden1_dim + hidden2_dim +
            output_dim * hidden2_dim + output_dim)


def fnv1a(data: bytes, value: int = 1469598103934665603) -> int:
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value
