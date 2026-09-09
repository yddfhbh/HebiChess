# HebiChess NNUE reference

`features.py` is the feature ABI reference.  It uses square indices `a1=0` through `h8=63`; Black vertically flips both king and piece squares and treats Black as own color.  The index is `((king * 12 + colored_type) * 64 + square)`, where `colored_type = (own ? 0 : 6) + (piece_type - 1)` and Pawn..King are 0..5. Kings are included, so the dimension is `64*12*64 = 49,152`.

The network is sparse transform `49152 -> 256`, two perspective accumulators concatenated in side-to-move order, then `512 -> 32 -> 32 -> 1`. Every hidden stage uses `clamp(x, 0, 1)`. Output is side-to-move centipawns (rounded in C++).

`reference.py` is the package-free validation oracle.  It has the same feature
formula, transform/linear row layouts, activation sequence, STM-first concat,
and serialization layout as `features.py`, `model.py`, and `export.py`.
Unlike those PyTorch training/export modules, it can run with Python's standard
library alone and explicitly rounds every arithmetic update through float32.

Run `python -m training.nnue.make_test_network /tmp/test.hebinnue` to create a
deterministic seed `20260909` validation network.  It is intentionally not a
training checkpoint and must not be committed.  To exercise the C++ engine,
run:

```
python -m training.nnue.parity --engine ./build-release/HebiChess
```

The parity runner checks 10 representative FENs in both perspectives, a
color-swapped/mirrored orientation relation, raw float inference (tolerance
`1e-4`), rounded centipawn output, and six malformed-file rejection cases.
The `nnueeval` diagnostic prints both the rounded search score and a raw float
score solely for this comparison.

The v1 binary has a 56-byte LE header: magic `HEBINNUE`, version, feature set,
dimensions, scalar=1(float32), endian marker `0x01020304`, parameter count,
and FNV-1a-64 payload checksum. Parameters follow in state-dict order
documented in `export.py`.
