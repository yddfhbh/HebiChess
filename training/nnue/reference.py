"""Pure-Python HalfKP-v1 and .hebinnue v1/v2/v3 validation reference.

This module deliberately uses only the standard library.  It is a file-format
and inference oracle, not a training implementation.
"""
from __future__ import annotations

from array import array
import math
import struct

from .format import (ENDIAN_MARKER, MAGIC, SCALAR_FLOAT32, V1_HEADER,
                     V2_HEADER, V3_HEADER, final_hidden_activation_name,
                     fnv1a, parameter_count)

INPUT_DIM = 64 * 12 * 64
ACCUMULATOR, HIDDEN1, HIDDEN2 = 256, 32, 32
FEATURE_SET_V1 = 1
HEADER = V1_HEADER  # Compatibility alias for legacy validation helpers.
PARAMETER_COUNT = (INPUT_DIM * ACCUMULATOR + ACCUMULATOR +
                   HIDDEN1 * (2 * ACCUMULATOR) + HIDDEN1 +
                   HIDDEN2 * HIDDEN1 + HIDDEN2 + HIDDEN2 + 1)
PIECE_TYPE = {"P": 0, "N": 1, "B": 2, "R": 3, "Q": 4, "K": 5}


def f32(value: float) -> float:
    """Round exactly through IEEE-754 binary32, like a stored C++ float."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def parse_fen(fen: str):
    fields = fen.split()
    if len(fields) < 2:
        raise ValueError("FEN needs placement and side to move")
    board = []
    ranks = fields[0].split("/")
    if len(ranks) != 8 or fields[1] not in ("w", "b"):
        raise ValueError("invalid FEN")
    for fen_rank, text in enumerate(ranks):
        rank, file = 7 - fen_rank, 0
        for piece in text:
            if piece.isdigit():
                file += int(piece)
            elif piece.upper() in PIECE_TYPE and file < 8:
                board.append((rank * 8 + file, piece))
                file += 1
            else:
                raise ValueError("invalid FEN placement")
        if file != 8:
            raise ValueError("invalid FEN rank width")
    return board, fields[1]


def active_features(fen: str, perspective: str) -> list[int]:
    """Physical a1..h8 ordered feature indices for white or black."""
    board, _ = parse_fen(fen)
    white = perspective.lower() == "white"
    king = next((square for square, piece in board
                 if piece == ("K" if white else "k")), None)
    if king is None:
        return []
    orient = (lambda square: square) if white else (lambda square: square ^ 56)
    king_square = orient(king)
    result = []
    for square, piece in sorted(board):
        colored_type = (0 if piece.isupper() == white else 6) + PIECE_TYPE[piece.upper()]
        result.append((king_square * 12 + colored_type) * 64 + orient(square))
    return result


class Network:
    def __init__(self, parameters: array, *, input_dim=INPUT_DIM,
                 accumulator=ACCUMULATOR, hidden1=HIDDEN1, hidden2=HIDDEN2,
                 output=1, output_scale=1.0, format_version=1,
                 final_hidden_activation="clipped_relu"):
        if (input_dim, accumulator, output) != (INPUT_DIM, ACCUMULATOR, 1):
            raise ValueError("unsupported feature/network dimensions")
        expected = parameter_count(input_dim, accumulator, hidden1, hidden2, output)
        if len(parameters) != expected:
            raise ValueError("wrong parameter count")
        if output_scale <= 0 or not math.isfinite(output_scale):
            raise ValueError("invalid output scale")
        self.parameters = parameters
        self.input_dim, self.accumulator = input_dim, accumulator
        self.hidden1_dim, self.hidden2_dim = hidden1, hidden2
        self.output_scale, self.format_version = output_scale, format_version
        if final_hidden_activation not in ("clipped_relu", "relu"):
            raise ValueError("unsupported final hidden activation")
        self.final_hidden_activation = final_hidden_activation
        offset = 0
        self.transform = offset; offset += input_dim * accumulator
        self.transform_bias = offset; offset += accumulator
        self.hidden1 = offset; offset += hidden1 * 2 * accumulator
        self.hidden1_bias = offset; offset += hidden1
        self.hidden2 = offset; offset += hidden2 * hidden1
        self.hidden2_bias = offset; offset += hidden2
        self.output = offset; offset += output * hidden2
        self.output_bias = offset

    def tensor_views(self):
        """Return serialized tensors as flat views, in documented payload order."""
        bounds = (
            ("transform.weight", self.transform, self.input_dim * self.accumulator),
            ("transform_bias", self.transform_bias, self.accumulator),
            ("hidden1.weight", self.hidden1, self.hidden1_dim * 2 * self.accumulator),
            ("hidden1.bias", self.hidden1_bias, self.hidden1_dim),
            ("hidden2.weight", self.hidden2, self.hidden2_dim * self.hidden1_dim),
            ("hidden2.bias", self.hidden2_bias, self.hidden2_dim),
            ("output.weight", self.output, self.hidden2_dim),
            ("output.bias", self.output_bias, 1),
        )
        return {name: self.parameters[start:start + count] for name, start, count in bounds}

    def evaluate(self, fen: str) -> float:
        white = self._accumulate(active_features(fen, "white"))
        black = self._accumulate(active_features(fen, "black"))
        _, side = parse_fen(fen)
        stm, opponent = (white, black) if side == "w" else (black, white)
        h1 = []
        for out in range(self.hidden1_dim):
            total = self.parameters[self.hidden1_bias + out]
            row = self.hidden1 + out * (2 * self.accumulator)
            for index in range(self.accumulator):
                total = f32(total + f32(self.parameters[row + index] * _clip(stm[index])))
            for index in range(self.accumulator):
                total = f32(total + f32(self.parameters[row + self.accumulator + index] * _clip(opponent[index])))
            h1.append(_clip(total))
        h2 = []
        for out in range(self.hidden2_dim):
            total = self.parameters[self.hidden2_bias + out]
            row = self.hidden2 + out * self.hidden1_dim
            for index in range(self.hidden1_dim):
                total = f32(total + f32(self.parameters[row + index] * h1[index]))
            h2.append(_clip(total) if self.final_hidden_activation == "clipped_relu"
                      else max(0.0, total))
        total = self.parameters[self.output_bias]
        for index in range(self.hidden2_dim):
            total = f32(total + f32(self.parameters[self.output + index] * h2[index]))
        if not math.isfinite(total):
            raise ValueError("non-finite network score")
        return total * self.output_scale

    def _accumulate(self, features: list[int]) -> list[float]:
        result = [self.parameters[self.transform_bias + i] for i in range(self.accumulator)]
        for feature in features:
            row = self.transform + feature * self.accumulator
            for index in range(self.accumulator):
                result[index] = f32(result[index] + self.parameters[row + index])
        return result


def _clip(value: float) -> float:
    return min(1.0, max(0.0, value))


def load(path) -> Network:
    with open(path, "rb") as source:
        prefix = source.read(12)
        if len(prefix) != 12:
            raise ValueError("NNUE file is truncated (header)")
        magic, version = struct.unpack("<8sI", prefix)
        if magic != MAGIC:
            raise ValueError("NNUE bad magic")
        header_struct = (V1_HEADER if version == 1 else V2_HEADER if version == 2
                         else V3_HEADER if version == 3 else None)
        if header_struct is None:
            raise ValueError("NNUE unsupported format version")
        header_data = prefix + source.read(header_struct.size - len(prefix))
        if len(header_data) != header_struct.size:
            raise ValueError("NNUE file is truncated (header)")
        header = header_struct.unpack(header_data)
        if version == 1:
            _, _, feature_set, inputs, accumulator, hidden1, hidden2, scalar, endian, count, checksum = header
            output, output_scale = 1, 1.0
            final_hidden_activation = "clipped_relu"
        elif version == 2:
            _, _, feature_set, inputs, accumulator, hidden1, hidden2, output, scalar, endian, output_scale, count, checksum = header
            final_hidden_activation = "clipped_relu"
        else:
            (_, _, feature_set, inputs, accumulator, hidden1, hidden2, output,
             scalar, endian, activation, output_scale, count, checksum) = header
            final_hidden_activation = final_hidden_activation_name(activation)
        if (feature_set, inputs, accumulator, output) != (FEATURE_SET_V1, INPUT_DIM, ACCUMULATOR, 1):
            raise ValueError("NNUE incompatible feature/network dimensions")
        expected = parameter_count(inputs, accumulator, hidden1, hidden2, output)
        if (scalar, endian, count) != (SCALAR_FLOAT32, ENDIAN_MARKER, expected):
            raise ValueError("NNUE unsupported scalar type, endian marker, or parameter count")
        if version == 1 and (hidden1, hidden2) != (HIDDEN1, HIDDEN2):
            raise ValueError("NNUE incompatible feature/network dimensions")
        if version == 2 and (hidden1, hidden2) != (128, 128):
            raise ValueError("NNUE v2 supports only the frozen 128/128 architecture")
        if version == 3 and (hidden1, hidden2) not in ((32, 32), (128, 128)):
            raise ValueError("NNUE v3 supports only 32/32 or 128/128 architectures")
        payload = source.read()
    if len(payload) != expected * 4:
        raise ValueError("NNUE file is truncated (parameters)" if len(payload) < expected * 4 else "NNUE file has trailing data")
    if fnv1a(payload) != checksum:
        raise ValueError("NNUE checksum mismatch")
    values = array("f")
    values.frombytes(payload)
    if struct.pack("=I", 1) != struct.pack("<I", 1):
        values.byteswap()
    return Network(values, input_dim=inputs, accumulator=accumulator, hidden1=hidden1,
                   hidden2=hidden2, output=output, output_scale=output_scale,
                   format_version=version,
                   final_hidden_activation=final_hidden_activation)
