"""Pure-Python HalfKP-v1 and .hebinnue v1 validation reference.

This module deliberately uses only the standard library.  It is a file-format
and inference oracle, not a training implementation.
"""
from __future__ import annotations

from array import array
import math
import struct

INPUT_DIM = 64 * 12 * 64
ACCUMULATOR, HIDDEN1, HIDDEN2 = 256, 32, 32
FEATURE_SET_V1 = 1
HEADER = struct.Struct("<8s8I2Q")
PARAMETER_COUNT = (INPUT_DIM * ACCUMULATOR + ACCUMULATOR +
                   HIDDEN1 * (2 * ACCUMULATOR) + HIDDEN1 +
                   HIDDEN2 * HIDDEN1 + HIDDEN2 + HIDDEN2 + 1)
PIECE_TYPE = {"P": 0, "N": 1, "B": 2, "R": 3, "Q": 4, "K": 5}


def f32(value: float) -> float:
    """Round exactly through IEEE-754 binary32, like a stored C++ float."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def fnv1a(data: bytes, value: int = 1469598103934665603) -> int:
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


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
    def __init__(self, parameters: array):
        if len(parameters) != PARAMETER_COUNT:
            raise ValueError("wrong parameter count")
        self.parameters = parameters
        offset = 0
        self.transform = offset; offset += INPUT_DIM * ACCUMULATOR
        self.transform_bias = offset; offset += ACCUMULATOR
        self.hidden1 = offset; offset += HIDDEN1 * 2 * ACCUMULATOR
        self.hidden1_bias = offset; offset += HIDDEN1
        self.hidden2 = offset; offset += HIDDEN2 * HIDDEN1
        self.hidden2_bias = offset; offset += HIDDEN2
        self.output = offset; offset += HIDDEN2
        self.output_bias = offset

    def evaluate(self, fen: str) -> float:
        white = self._accumulate(active_features(fen, "white"))
        black = self._accumulate(active_features(fen, "black"))
        _, side = parse_fen(fen)
        stm, opponent = (white, black) if side == "w" else (black, white)
        h1 = []
        for out in range(HIDDEN1):
            total = self.parameters[self.hidden1_bias + out]
            row = self.hidden1 + out * (2 * ACCUMULATOR)
            for index in range(ACCUMULATOR):
                total = f32(total + f32(self.parameters[row + index] * _clip(stm[index])))
            for index in range(ACCUMULATOR):
                total = f32(total + f32(self.parameters[row + ACCUMULATOR + index] * _clip(opponent[index])))
            h1.append(_clip(total))
        h2 = []
        for out in range(HIDDEN2):
            total = self.parameters[self.hidden2_bias + out]
            row = self.hidden2 + out * HIDDEN1
            for index in range(HIDDEN1):
                total = f32(total + f32(self.parameters[row + index] * h1[index]))
            h2.append(_clip(total))
        total = self.parameters[self.output_bias]
        for index in range(HIDDEN2):
            total = f32(total + f32(self.parameters[self.output + index] * h2[index]))
        if not math.isfinite(total):
            raise ValueError("non-finite network score")
        return total

    def _accumulate(self, features: list[int]) -> list[float]:
        result = [self.parameters[self.transform_bias + i] for i in range(ACCUMULATOR)]
        for feature in features:
            row = self.transform + feature * ACCUMULATOR
            for index in range(ACCUMULATOR):
                result[index] = f32(result[index] + self.parameters[row + index])
        return result


def _clip(value: float) -> float:
    return min(1.0, max(0.0, value))


def load(path) -> Network:
    with open(path, "rb") as source:
        header_data = source.read(HEADER.size)
        if len(header_data) != HEADER.size:
            raise ValueError("NNUE file is truncated (header)")
        header = HEADER.unpack(header_data)
        magic, version, feature_set, inputs, accumulator, hidden1, hidden2, scalar, endian, count, checksum = header
        if magic != b"HEBINNUE": raise ValueError("NNUE bad magic")
        if version != 1: raise ValueError("NNUE unsupported format version")
        if (feature_set, inputs, accumulator, hidden1, hidden2) != (FEATURE_SET_V1, INPUT_DIM, ACCUMULATOR, HIDDEN1, HIDDEN2):
            raise ValueError("NNUE incompatible feature/network dimensions")
        if (scalar, endian, count) != (1, 0x01020304, PARAMETER_COUNT):
            raise ValueError("NNUE unsupported scalar type, endian marker, or parameter count")
        payload = source.read()
    if len(payload) != PARAMETER_COUNT * 4:
        raise ValueError("NNUE file is truncated (parameters)" if len(payload) < PARAMETER_COUNT * 4 else "NNUE file has trailing data")
    if fnv1a(payload) != checksum: raise ValueError("NNUE checksum mismatch")
    values = array("f")
    values.frombytes(payload)
    if struct.pack("=I", 1) != struct.pack("<I", 1): values.byteswap()
    return Network(values)
