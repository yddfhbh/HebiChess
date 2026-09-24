# QSearch fixed move-buffer experiment

This experiment is deliberately independent of the rejected fused legality / check-metadata
candidate.  All four targets below explicitly compile
`HEBICHESS_QSEARCH_FUSED_CHECKS=0`; production `HebiChess` is unchanged.

| target | `HEBICHESS_QSEARCH_FIXED_MOVE_LIST` | `HEBICHESS_QSEARCH_PROFILE` |
| --- | ---: | ---: |
| `HebiChessSearchQsearchMoveBufferBaseline` | 0 | 0 |
| `HebiChessSearchQsearchMoveBufferCandidate` | 1 | 0 |
| `HebiChessSearchQsearchMoveBufferBaselineProfile` | 0 | 1 |
| `HebiChessSearchQsearchMoveBufferCandidateProfile` | 1 | 1 |

The candidate creates pseudo moves in a `FixedMoveList`, filters legal moves in-place, then
constructs and sorts a `FixedOrderedMoveList`.  The capacity is 416: a conservative geometric
pseudo-legal upper bound of `15 * 27 + 10`, for 15 non-king pieces and a king.  An overflow is an
assertion failure followed by `abort`, including release builds.

## VM profile smoke (not a performance decision)

The canonical 10-FEN fixed-depth HCE smoke at depth 4 had exact equality for `bestmove`, score,
completed depth, nodes, and qnodes.  It reported these aggregate profile counters:

| counter | vector baseline | fixed candidate |
| --- | ---: | ---: |
| vector allocations | 164,327 | 0 |
| vector reallocations | 102,072 | 0 |
| estimated allocated bytes | 5,130,368 | 0 |
| maximum tactical moves | 77 | 77 |
| maximum evasion moves | 78 | 78 |

The VM result is only an allocation/correctness smoke.  Profile clocks perturb execution, so it
does not establish an NPS win and the candidate remains unpromoted pending Windows native and
WASM A/B results.

## Stack / frame acceptance gate

The candidate deliberately keeps one `FixedMoveList` and one `FixedOrderedMoveList` live while
ordering a QSearch node: the ordered list is built from the still-needed legal list. The evasion
pair is scoped inside `if (in_check)` and returns before the tactical pair is created, so evasion
and tactical buffers do not add together in one invocation. Benchmark JSON records the exact
target-compiled `sizeof(Move)`, `sizeof(OrderedMove)`, `sizeof(FixedMoveList)`,
`sizeof(FixedOrderedMoveList)`, and their simultaneously-live pair byte total.

For a compiler frame report, configure the diagnostic Emscripten build with
`-DCMAKE_CXX_FLAGS=-fstack-usage` and inspect the generated `.su` row for `quiescence_impl`.
Record baseline/candidate frame bytes beside `q_max_ply`; both use `-sSTACK_SIZE=2097152`.
No stack abort alone is acceptance evidence.

## Required Windows decision run

Use the normal (non-profile) baseline and candidate binaries, with the same NNUE network and
canonical fixture.  Run baseline-first and candidate-first at least twice, and retain per-FEN
NPS, completed depth, qnodes, wall/search time, and parity output.  Build the corresponding Node
WASM variants with the same H1=8, 2 MiB stack, QSearch/QTT configuration and 1000ms suite,
including checked-evasion.  Promote only if both orders show a consistent whole-search gain and
the WASM result improves too; otherwise reject this experiment.

The native runner executes both orders for every repetition (minimum two):

```bat
cmake --build build-release --config Release --target HebiChessSearchQsearchMoveBufferBaseline HebiChessSearchQsearchMoveBufferCandidate -j 1
py -3 scripts\benchmark-qsearch-move-buffer-ab.py --baseline build-release\HebiChessSearchQsearchMoveBufferBaseline.exe --candidate build-release\HebiChessSearchQsearchMoveBufferCandidate.exe --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --repetitions 2 --output runs\native-qsearch-move-buffer-ab.json
```

## Dedicated WASM A/B

`HebiChessWasmQsearchMoveBufferBaseline` and `HebiChessWasmQsearchMoveBufferCandidate` are
Node-only, production-equivalent H1=8 artifacts. Their only behavior difference is
`HEBICHESS_QSEARCH_FIXED_MOVE_LIST`; both pin QTT variant 2/cutoff mask 7, delta pruning on,
lazy/fused checks off, and the same 2 MiB stack.

```bat
call %EMSDK%\emsdk_env.bat
emcmake cmake -S . -B build-wasm-move-buffer -DCMAKE_BUILD_TYPE=Release -DHEBICHESS_BUILD_WASM_MOVE_BUFFER_AB=ON
cmake --build build-wasm-move-buffer --config Release --target HebiChessWasmQsearchMoveBufferBaseline HebiChessWasmQsearchMoveBufferCandidate -j 1
node scripts\benchmark-wasm-qsearch-move-buffer-ab.js --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --depth 5 --time-ms 1000 --order baseline-first --output runs\wasm-move-buffer-baseline-first.json
node scripts\benchmark-wasm-qsearch-move-buffer-ab.js --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --depth 5 --time-ms 1000 --order candidate-first --output runs\wasm-move-buffer-candidate-first.json
```

Repeat each order twice. The JSON includes exact fixed-depth nodes/qnodes parity, fixed-time
mismatches, per-FEN and aggregate NPS, median relative NPS, completed depth, qnodes, wall time,
q_max_ply, and qprofile timing/allocation counters when a separate profile build is used.

For checked-evasion first build with
`-DHEBICHESS_BUILD_WASM_MOVE_BUFFER_AB_DIAGNOSTIC=ON`, place it in a separate output directory,
then add `--only checked-evasion --verbose-progress` to the runner. For profile-only breakdowns,
use a separate build with `-DHEBICHESS_BUILD_WASM_MOVE_BUFFER_AB_PROFILE=ON`; never use its NPS
as promotion evidence.

## Final Windows decision — REJECT

Candidate: `9f8c11a52a7e8a0720382c506bed7215800afcae`

Windows native fixed-depth correctness had exact parity.  Whole-search performance, however,
showed no stable improvement.

Windows WASM had zero fixed-depth mismatches.  `q_max_ply` was 20 in fixed-depth runs and 29 in
fixed-time runs.  Aggregate candidate deltas were approximately:

| suite | aggregate candidate delta |
| --- | --- |
| fixed-depth | -5.25%, +0.51%, -0.92%, -5.32% |
| fixed-time | -0.74%, +0.25%, -0.06%, -0.24% |

Removing allocations succeeded, but it did not improve actual native or WASM search throughput.
This experiment is therefore **REJECTED**: do not promote to production, merge to `main`, or
deploy it.  Keep the rejected fused-check path and fixed move-buffer path disabled.  No further
benchmarking is required for this experiment.
