# NNUE backend follow-up design

`evaluate(const Board&)` remains the HCE-only search entry point.  It calls
`evaluate_hce(const Board&)`; this makes the existing hot path independent of
UCI mode selection.  `evaluate(const Board&, EvalMode)` is the explicit
backend API for setup and tests.  It returns no value for NNUE until a
validated network is available.

## Incremental accumulator hooks

The future accumulator belongs beside the board state, but it should not be
added until a network format fixes the accumulator dimensions and layout.
`Board::make_move()` in `src/chess/board.cpp` is the single forward hook: emit
piece-square removals and additions before each `set_piece()` call, recording
the inverse operations in `UndoState` (or an evaluator-owned parallel undo
stack).  `Board::unmake_move()` restores the same saved accumulator state
after it restores pieces.  A null move needs no feature change, only its usual
side-to-move state handling.

The exact deltas are: ordinary move (remove `from`, add `to`); capture
(remove captured `to`, then moving delta); en passant (remove captured pawn at
`captured_square`, then moving delta); promotion (remove pawn at `from`, add
promoted piece at `to`); promotion capture (also remove target); castling
(king delta plus rook `a/h` to `d/f` delta); and any king move (the king
square itself changes the king-relative feature plane, so rebuild that
perspective's accumulator rather than trying to transform every feature).

## Fixed feature ABI: HalfKP-v1

HalfKP-v1 has exactly `64 king squares * 12 colored piece types * 64 board
squares = 49,152` binary features per perspective. Its index is
`((oriented_king * 12 + colored_type) * 64 + oriented_piece_square)`.
Squares use `a1=0 .. h8=63`. White leaves squares unchanged; Black applies
`square ^ 56` (vertical rank flip only) to its king and piece squares. The
perspective king is that side's own king. Colors normalize to own=0 and
opponent=1; Pawn..King are types 0..5, so
`colored_type=(own ? 0 : 6)+piece_type`. Kings, including the perspective king,
are included. Side to move is not a feature; it selects accumulator order.

The float32 reference network computes sparse `49152 -> 256` transform sums
plus bias for each perspective, concatenates `[STM, opponent]`, then uses
`512 -> H1 -> H2 -> 1`. Accumulator and hidden1 are always `clamp(x, 0, 1)`.
v1/v2 and v3 enum 1 use clipped final hidden2; v3 enum 2 uses `max(x, 0)`.
Output is side-to-move centipawns, multiplied by the v2/v3 header's positive
`output_scale` before C++ rounds it. No SIMD, quantization, or incremental
accumulator exists in any format.

`.hebinnue` v1 is little-endian and starts with a packed 56-byte header:
`HEBINNUE` magic, version, feature set, dimensions, scalar=float32 identifier,
endian marker `0x01020304`, parameter count, and FNV-1a-64 payload checksum.
Payload order is transform `[49152][256]`, bias, hidden1 `[32][512]`, bias,
hidden2 `[32][32]`, bias, output `[32]`, bias. Loader rejects malformed magic,
version/dimensions, scalar/endian mismatch, checksum mismatch, truncation, and
trailing data.

`.hebinnue` v2 uses the exact 64-byte Python `<8s9If2Q` header: magic,
format version, feature ABI, input/accumulator/hidden1/hidden2/output
dimensions, scalar type, endian marker, float `output_scale`, parameter count,
and FNV-1a-64 payload checksum. It is deliberately restricted to the frozen
`49152 -> 256 -> 128 -> 128 -> 1` architecture. The native loader dispatches
by the shared magic/version prefix; v1 remains its original 32/32 layout.

`.hebinnue` v3 uses the exact 68-byte `<8s10If2Q>` header. It is v2's header
plus `final_hidden_activation` before `output_scale`: `1` is
`CLIPPED_RELU_0_1`; `2` is `RELU`. Payload tensor order and raw float32 bytes
are unchanged. The loader stores this activation and dispatches only final
hidden2. It rejects unknown activation enums, unknown format versions,
malformed sizes/counts/checksums, truncation, and trailing bytes. v1/v2 always
load internally as clipped ReLU and are never reinterpreted.

Exporter policy is deterministic: clipped 32/32 selects v1; only the approved
metadata-less legacy clipped 128/128 checkpoint selects v2; all new 128/128
checkpoints require explicit final-hidden metadata and select v3. Explicit
clipped 128/128 is v3 too. Top-level and `pilot_config` metadata disagreement
rejects export.

Do not put root style or aggression terms into these features: network output
remains objective and root policy stays separate.

## Phase 6-3C-2 layout diagnostics

The normal native and browser builds are pinned to `Original4`.  They expose
no UCI option, environment variable, or browser toggle for layout selection.
`Prepacked4` and `Prepacked8` are test-only derived hidden1 weight layouts;
the `.hebinnue` bytes and parser are unchanged.

The same source tree builds these native diagnostic targets on Linux, Windows,
and macOS:

```
HebiChessSearchBaselineOriginal4
HebiChessSearchBaselinePrepacked4
HebiChessSearchBaselinePrepacked8
```

For example, a Windows Release build can use:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target HebiChessSearchBaselineOriginal4 HebiChessSearchBaselinePrepacked4 HebiChessSearchBaselinePrepacked8
build\\Release\\HebiChessSearchBaselineOriginal4.exe --network frozen.hebinnue --depth 5 --time-ms 0 --output original4.json --summary original4.txt
```

Every target accepts `--depth`, `--time-ms`, `--network`, `--output`, and
`--summary` (plus optional `--fixture` and `--rounds`).  Its JSON includes
best move, score, nodes, qnodes, NPS, and active-versus-Original4 hidden1/raw
and centipawn parity.  `--time-ms 0` means an unbounded fixed-depth run.

For Node WASM evaluator A/B, configure with
`-DHEBICHESS_BUILD_WASM_NNUE_LAYOUT_VARIANTS=ON` through `emcmake cmake` and
build `HebiChessWasmNnueOriginal4`, `HebiChessWasmNnuePrepacked4`, and
`HebiChessWasmNnuePrepacked8`.  The artifacts go under
`build-wasm/nnue-layouts`, never `web/public`.  Run
`scripts/benchmark-wasm-nnue-layouts.js` with the model, FEN fixture, and the
three generated `.js` paths to compare raw evaluator throughput and checksums.
