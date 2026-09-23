# QSearch profiling checkpoint

This checkpoint keeps the `d187507` search semantics unchanged. It adds
production-light `uint64_t` counters to `SearchResult`, emits a separate UCI
`info string qprofile ...` line, and logs that line in web-v2 as
`[JJUGLE QPROFILE]`. The UI does not render these fields.

The fixed native fixture is `tests/data/phase6-search-baseline.fen` and now
contains ten positions covering opening, quiet/tactical middlegames, check
evasions, capture-heavy play, and an endgame. Run the production configuration
with:

```sh
cmake --build build-release --target HebiChessSearchBaseline -j2
build-release/HebiChessSearchBaseline \
  --fixture tests/data/phase6-search-baseline.fen \
  --network /path/to/hebinnue \
  --modes nnue --time-ms 1000,3000 \
  --output runs/qsearch-profile.json \
  --summary runs/qsearch-profile.summary.txt
```

The JSON records depth, attempted depth, nodes, qnodes, qnode ratio, NPS,
bestmove, score, and all QSearch counters. Delta pruning, persistent QTT, and
NNUE remain enabled; no pruning threshold, evaluation, style, or time
management rule is changed here.

An initial depth-3 spot check on `many-captures` produced 734 qnodes out of
the total, 95 in-check qnodes, 110 tactical moves generated and 76 searched,
2 SEE prunes, 16 delta prunes, and 150/128 QTT hits/cutoffs. The same position
with NNUE enabled counted 434 qsearch NNUE evaluations and 525 incremental
updates. These are instrumentation smoke values, not substitutes for the
1,000/3,000 ms fixed-time suite.

The first bottleneck candidates are therefore (1) in-check/evasion work in
tactical positions, (2) tactical move generation and check detection, and
(3) NNUE evaluation/update volume. The fixed-time JSON should be used to rank
them across all ten positions before implementing any optimization.
