"""Versioned, sparse training-set format for HebiChess NNUE v1.

Each fixed-size record stores two lists of uint16 HalfKP feature indices, the
side to move, a clamped centipawn teacher target, mate distance metadata, and
a source-game id.  It deliberately stores no dense 49,152-element vectors.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Iterable, Iterator

from .features import FEATURE_SET_V1, INPUT_DIM, both_features, parse_fen

MAGIC = b"HEBIDAT1"
VERSION = 1
MAX_FEATURES = 32
HEADER = struct.Struct("<8sIIIIQ")  # magic, version, feature ABI, record size, flags, count
RECORD = struct.Struct("<IihBBB32H32H")


@dataclass(frozen=True)
class Sample:
    white: tuple[int, ...]
    black: tuple[int, ...]
    side: int                 # 0 = White, 1 = Black
    target: int               # side-to-move centipawns, already clamped
    mate: int = 0             # signed mate distance; zero means ordinary cp
    game_id: int = 0


def sample_from_fen(fen: str, target: int, *, mate: int = 0, game_id: int = 0) -> Sample:
    white, black = both_features(fen)
    _, side = parse_fen(fen)
    return Sample(tuple(white), tuple(black), 0 if side == "w" else 1, int(target), int(mate), int(game_id))


def _validate(sample: Sample) -> None:
    if len(sample.white) > MAX_FEATURES or len(sample.black) > MAX_FEATURES:
        raise ValueError("a chess position cannot have more than 32 active features")
    if sample.side not in (0, 1): raise ValueError("side must be 0 (white) or 1 (black)")
    if not -(2**31) <= sample.target < 2**31: raise ValueError("target out of int32 range")
    if not -(2**15) <= sample.mate < 2**15: raise ValueError("mate out of int16 range")
    if not 0 <= sample.game_id < 2**32: raise ValueError("game id out of uint32 range")
    if any(not 0 <= x < INPUT_DIM for x in (*sample.white, *sample.black)):
        raise ValueError("feature index outside HalfKP-v1 ABI")


def write(path: str | Path, samples: Iterable[Sample]) -> int:
    """Write samples atomically enough for normal local use, returning count."""
    path = Path(path)
    count = 0
    with path.open("wb") as output:
        output.write(HEADER.pack(MAGIC, VERSION, FEATURE_SET_V1, RECORD.size, 0, 0))
        for sample in samples:
            _validate(sample)
            white = sample.white + (0,) * (MAX_FEATURES - len(sample.white))
            black = sample.black + (0,) * (MAX_FEATURES - len(sample.black))
            output.write(RECORD.pack(sample.game_id, sample.target, sample.mate, sample.side,
                                     len(sample.white), len(sample.black), *white, *black))
            count += 1
        output.seek(0)
        output.write(HEADER.pack(MAGIC, VERSION, FEATURE_SET_V1, RECORD.size, 0, count))
    return count


class Reader:
    def __init__(self, path: str | Path):
        self.path = Path(path)
        self._source = self.path.open("rb")
        header = self._source.read(HEADER.size)
        if len(header) != HEADER.size: self.close(); raise ValueError("dataset truncated (header)")
        magic, version, feature_set, record_size, flags, self.count = HEADER.unpack(header)
        if magic != MAGIC: self.close(); raise ValueError("dataset bad magic")
        if version != VERSION: self.close(); raise ValueError("dataset unsupported format version")
        if feature_set != FEATURE_SET_V1: self.close(); raise ValueError("dataset incompatible feature ABI")
        if record_size != RECORD.size or flags != 0: self.close(); raise ValueError("dataset unsupported record layout")
        expected = HEADER.size + self.count * RECORD.size
        if self.path.stat().st_size != expected:
            self.close(); raise ValueError("dataset truncated or has trailing data")

    def close(self):
        if getattr(self, "_source", None): self._source.close(); self._source = None

    def __enter__(self): return self
    def __exit__(self, *_): self.close()
    def __len__(self): return self.count

    def __getitem__(self, index: int) -> Sample:
        if not 0 <= index < self.count: raise IndexError(index)
        self._source.seek(HEADER.size + index * RECORD.size)
        values = RECORD.unpack(self._source.read(RECORD.size))
        game_id, target, mate, side, nw, nb, *indices = values
        if side not in (0, 1) or nw > MAX_FEATURES or nb > MAX_FEATURES:
            raise ValueError("dataset invalid record")
        white, black = tuple(indices[:nw]), tuple(indices[MAX_FEATURES:MAX_FEATURES + nb])
        sample = Sample(white, black, side, target, mate, game_id)
        _validate(sample)
        return sample

    def __iter__(self) -> Iterator[Sample]:
        for index in range(self.count): yield self[index]


def collate(samples: list[Sample]):
    """Return packed Python lists; train.py turns them into device tensors."""
    white, black, white_offsets, black_offsets, sides, targets = [], [], [], [], [], []
    for sample in samples:
        white_offsets.append(len(white)); black_offsets.append(len(black))
        white.extend(sample.white); black.extend(sample.black)
        sides.append(sample.side); targets.append(sample.target)
    return white, white_offsets, black, black_offsets, sides, targets
