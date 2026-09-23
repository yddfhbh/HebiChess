# WASM H1=4 versus H1=8 A/B

This is a Windows acceptance benchmark for the frozen v3 NNUE model. It does
not replace the browser production artifact and does not change the production
default: `HebiChessWasm` remains H1=4.

The two test-only Emscripten targets are:

- `HebiChessWasmH1Interleaved4` → `build-wasm/h1-ab/h1-4/hebichess.js`
- `HebiChessWasmH1Interleaved8` → `build-wasm/h1-ab/h1-8/hebichess.js`

They have the same sources, `-O3`, memory flags, Node module factory name,
exported functions, production QSearch definitions, `HEBICHESS_QSEARCH_LAZY_CHECKS=0`,
and NNUE/network format. The sole variant-specific compile definition is
`HEBICHESS_NNUE_HIDDEN1_VARIANT=4` or `=8`.

`hebichess_nnue_benchmark_fens` is exported only from these test artifacts. It
runs the timed evaluator loops inside WASM: existing-accumulator evaluation and
incremental update plus evaluation. It is not exported from the browser
production artifact.

## Windows build and run

Use the frozen model whose SHA256 is
`4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826`.

```bat
call %EMSDK%\emsdk_env.bat
emcmake cmake -S . -B build-wasm-h1-ab -DCMAKE_BUILD_TYPE=Release -DHEBICHESS_BUILD_WASM_H1_AB=ON
cmake --build build-wasm-h1-ab --config Release --target HebiChessWasmH1Interleaved4 HebiChessWasmH1Interleaved8 -j 1
node scripts\benchmark-wasm-h1-ab.js --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --output runs\wasm-h1-ab.json
```

The default run uses the tracked 100-FEN raw NNUE corpus, the tracked 10-FEN
search fixture, fixed depth 5, fixed time 1000 ms, and 32 evaluator repeats.
For a longer Windows-only evaluator sample, pass `--eval-repeats 128`.

## Stack-overflow diagnosis

The test-only diagnostic artifact adds `-sASSERTIONS=2` and
`-sSTACK_OVERFLOW_CHECK=2`; browser production and the ordinary Node test
artifact never receive these flags.  Build it into a separate output directory
so it cannot be mistaken for an ordinary A/B measurement:

```bat
call %EMSDK%\emsdk_env.bat
emcmake cmake -S . -B build-wasm-h1-ab-diagnostic -DCMAKE_BUILD_TYPE=Release -DHEBICHESS_BUILD_WASM_H1_AB=ON -DHEBICHESS_BUILD_WASM_H1_AB_DIAGNOSTIC=ON -DHEBICHESS_WASM_H1_AB_OUTPUT_DIR=build-wasm/h1-ab-diagnostic
cmake --build build-wasm-h1-ab-diagnostic --config Release --target HebiChessWasmH1Interleaved4 HebiChessWasmH1Interleaved8 -j 1
node scripts\benchmark-wasm-h1-ab.js --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --h1-4 build-wasm\h1-ab-diagnostic\h1-4\hebichess.js --h1-8 build-wasm\h1-ab-diagnostic\h1-8\hebichess.js --depth 1 --time-ms 1000 --eval-repeats 1 --only checked-evasion --verbose-progress --output runs\wasm-h1-ab-checked-evasion-diagnostic.json
```

`--only <position-name>` restricts both search modes to one named canonical
fixture (for example, `checked-evasion`). `--verbose-progress` prints a start
and completion marker around every search, so an Emscripten abort can be tied
to an exact variant, search mode, and position. `--order h1-4-first` (default)
or `--order h1-8-first` reverses measurement order while retaining the actual
artifact-to-`h1_4`/`h1_8` mapping in JSON.

## Output format

`runs/wasm-h1-ab.json` has schema `hebichess-wasm-h1-ab-v1` and contains:

- `correctness.raw_nnue_parity`: 100-position exact raw and rounded-CP counts.
- `correctness.nnue_hard_fail_behavior`: v3 activation, checksum, and truncated-model rejection checks for both artifacts.
- `evaluator_microbench.h1_4` and `.h1_8`: positions, transitions, repeats, and the two microseconds-per-evaluation metrics.
- `search.fixed_depth.records`: per-position best move, score, completed depth, nodes, qnodes, NPS, and duplicate-bestmove/protocol status. This is the pass/fail search parity gate.
- `search.fixed_time.records`: the same per-position fields for the 1000 ms performance sample. Node count and completed depth are intentionally reported rather than required to match.

The process exits nonzero for raw or rounded NNUE parity failure, NNUE
hard-fail regression, malformed/duplicate bestmove output, or a fixed-depth
bestmove/score/depth mismatch. It records, but does not make the
performance sample fail solely for, a fixed-time result difference caused by
the variant completing different work before its deadline.
