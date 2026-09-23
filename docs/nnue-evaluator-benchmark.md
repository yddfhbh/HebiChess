# NNUE evaluator benchmark

`HebiChessNnueHidden1DenseLegacy`, `HebiChessNnueHidden1DenseInterleaved2`,
`HebiChessNnueHidden1DenseInterleaved4`, and
`HebiChessNnueHidden1DenseInterleaved8` compile the four hidden1 dense-loop
variants without changing the production engine.  The regular engine remains
the compile-time interleaved-4 variant.

Run these on Windows only.  Do not treat VM timing as a performance result.
From the repository root after an optimized Windows build, run the same
command for every variant:

```powershell
build\\Release\\HebiChessNnueHidden1DenseLegacy.exe --network hebinnue-v3-4c815d54bc6c9fbf.hebinnue --corpus tests/data/wasm-parity-100.fen --repeats 128
build\\Release\\HebiChessNnueHidden1DenseInterleaved2.exe --network hebinnue-v3-4c815d54bc6c9fbf.hebinnue --corpus tests/data/wasm-parity-100.fen --repeats 128
build\\Release\\HebiChessNnueHidden1DenseInterleaved4.exe --network hebinnue-v3-4c815d54bc6c9fbf.hebinnue --corpus tests/data/wasm-parity-100.fen --repeats 128
build\\Release\\HebiChessNnueHidden1DenseInterleaved8.exe --network hebinnue-v3-4c815d54bc6c9fbf.hebinnue --corpus tests/data/wasm-parity-100.fen --repeats 128
```

The output is one stable space-separated `key=value` record.  Timings are the
median of five runs in microseconds per evaluation.  `accumulator_rebuild`,
`clip`, `hidden1_dense`, `hidden1_activation`, `hidden2_dense`, and `output`
come from `profile_nnue_evaluator_stages()`.  `full_rebuild_evaluate` includes
an accumulator rebuild plus forward pass; `existing_accumulator_evaluate`
times only the forward pass; `incremental_update` times only the child
accumulator update; and `incremental_update_evaluate` is their composed cost.
The ordinary, capture, and king-refresh fields split incremental-update timing
by legal transition type.  `king_refresh` refreshes the moved king's
perspective accumulator.

Before timing, the harness verifies all 100 corpus positions against the
legacy hidden1 implementation. `hidden1_max_abs_diff`,
`final_raw_max_abs_diff`, and `final_cp_mismatch_count` report selected-loop
parity. The `incremental_*` parity fields compare each legal child transition
with a rebuilt child accumulator. A non-zero result that violates the printed
tolerances exits with failure. Use `--verify-only` to perform only those
checks; CTest runs it for all four variants with the deterministic v3 network.
