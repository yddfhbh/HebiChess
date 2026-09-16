# Phase 6-1 search baseline

`HebiChessSearchBaseline` calls the native `search()` and evaluator APIs directly; it does not scrape UCI output.  It produces a machine-readable JSON report and a compact text summary.  The output defaults to the ignored/generated `runs/` directory and must not be committed.

The tracked seven-position suite is `tests/data/phase6-search-baseline.fen`.  It reuses exact FENs from `tests/data/wasm-parity-100.fen`: the historical 24-position Phase 4 fixture selected source entries 38, 56, and 62 for zero-based critical indices 9, 12, and 13.  Index 9 is also the Phase 5 browser NNUE smoke FEN.

The harness runs each position with cleared TT and search heuristics, so each record is independent.  Fixed-depth runs compare equal nominal depth; fixed-time runs use an actual deadline and report completed depth.  `profile_style_metadata` is enabled only for this diagnostic executable: it adds root-only timing, never per-node clocks.

```bash
cmake -S . -B build-phase6 -DCMAKE_BUILD_TYPE=Release
cmake --build build-phase6 --target HebiChessSearchBaseline -j
./build-phase6/HebiChessSearchBaseline --depth 5 --time-ms 1000,3000 \
  --output runs/phase6-search-baseline.json
```

Without a model, HCE records are still produced and NNUE search/microbench entries are explicitly `unavailable`; NNUE is never allowed to fall back to HCE.  To run the frozen model comparison on Windows:

Baseline-server validation passed all 12 Torch-independent Python NNUE tests.  The three existing `training.nnue.test_model` tests were not run because Torch is unavailable on that server.  This Phase 6-1 change does not modify Python model or training code, so it does not add or install that dependency.

```powershell
$network = "runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue"
$expected = "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826"
if ((Get-FileHash $network -Algorithm SHA256).Hash.ToLower() -ne $expected) { throw "unexpected frozen model SHA256" }
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release --target HebiChessSearchBaseline
.\build-release\Release\HebiChessSearchBaseline.exe --depth 5 --time-ms 1000,3000 --network $network --output "runs\phase6-search-baseline.json"
```

The model must have SHA256 `4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826`; the harness intentionally does not download or create it.

Each search record contains elapsed time, nodes, main/qnode split and ratio, NPS, TT/SEE/null/LMR/PVS/aspiration counters and rates, plus objective and root-style verification timing/counters.  The evaluator microbench warm-ups outside timing, then reports median microseconds/evaluation over multiple samples and NNUE/HCE slowdown when both are available.
