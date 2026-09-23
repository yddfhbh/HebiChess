# WASM H1=4 versus H1=8 A/B

This is a Windows acceptance benchmark for the frozen v3 NNUE model. It does
not replace the browser production artifact and does not change the production
default: `HebiChessWasm` remains H1=4.

The two test-only Emscripten targets are:

- `HebiChessWasmH1Interleaved4` → `build-wasm-h1-ab/h1-ab/h1-4/hebichess.js`
- `HebiChessWasmH1Interleaved8` → `build-wasm-h1-ab/h1-ab/h1-8/hebichess.js`

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
emcmake cmake -S . -B build-wasm-h1-ab -DCMAKE_BUILD_TYPE=Release -DHEBICHESS_BUILD_WASM_H1_AB=ON -DHEBICHESS_WASM_H1_AB_OUTPUT_DIR=build-wasm-h1-ab/h1-ab
cmake --build build-wasm-h1-ab --config Release --target HebiChessWasmH1Interleaved4 HebiChessWasmH1Interleaved8 -j 1
node scripts\benchmark-wasm-h1-ab.js --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --output runs\wasm-h1-ab.json
```

The default run uses the tracked 100-FEN raw NNUE corpus, the tracked 10-FEN
search fixture, fixed depth 5, fixed time 1000 ms, and 32 evaluator repeats.
For a longer Windows-only evaluator sample, pass `--eval-repeats 128`.

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
