# QSearch transposition table — Phase 6-4B

## Final status: PASS

The normal `HebiChess` production target now uses the accepted Phase 6-4B
search configuration.  It is compile-time fixed; there is no UCI option or
runtime toggle.

| Setting | Production value |
| --- | ---: |
| `HEBICHESS_QSEARCH_DELTA_PRUNING` | `1` |
| `HEBICHESS_QSEARCH_TT_VARIANT` | `2` (active accepted variant) |
| `HEBICHESS_QSEARCH_TT_CUTOFF_MASK` | `7` (`Exact|Lower|Upper`) |
| `HEBICHESS_QSEARCH_TT_PROFILE` | `0` |
| `HEBICHESS_QSEARCH_TT_DIAGNOSTIC` | `0` |

Apart from `HEBICHESS_STRENGTH_RUNNER=1`, which only enables strength-runner
metadata, these relevant definitions are identical to
`HebiChessStrengthNoDeltaQtt`.  The normal timed path therefore has active
QTT cutoffs with neither QTT profiling nor diagnostic/oracle instrumentation.
The existing separately named diagnostic and A/B targets are retained.

## Root cause and accepted fix

The legacy alpha-dependent delta-pruning rejection made qsearch results unsafe
for position-only QTT reuse across different alpha/beta windows.  The accepted
fix removes that legacy rejection and enables QTT; it introduces no replacement
delta margin or formula.  QSearch TT behavior and the rest of NNUE/search
behavior are otherwise unchanged.

QTT uses its separate table at non-check qsearch nodes only, with existing TT
score/bound conventions and no QTT move ordering.  Its lifetime spans the
game like the main TT and it is cleared by `clear_transposition_table()`.

## Acceptance evidence

Windows frozen correctness found all 270/270 no-delta QTT OFF/ON comparisons
identical for best move and score.  The accepted Windows performance result
showed the combined no-delta + QTT configuration approximately 6.1% faster
than the old current search.

The frozen strength suite was `phase6-4b-7c-book500-v1`, SHA-256
`c942fad3c5bbc259758e0c264ff6dc4650aefa94e4130377c699e2de21fb460b`:

| Measure | Result |
| --- | --- |
| Games | 1000 (500 unique openings with reversed colors) |
| W/D/L from candidate perspective | 416 / 168 / 416 |
| Score | 50.00% (95% CI 47.9144%–52.0856%) |
| Elo | +0.0 (95% CI -14.5006 to +14.5006) |
| Failures | 0 crashes, 0 timeouts, 0 protocol failures |

This result is strength-neutral within measured uncertainty / faster.  It does
not establish or claim an Elo gain.

## Timing and regression guardrails

Production objective timing remains exactly:

```cpp
objective_deadline =
    limits.has_deadline
        ? limits.deadline - std::chrono::milliseconds(10)
        : limits.deadline;
```

There is no 150 ms reserve, no remaining-time / 4 clamp, and no new timing
behavior in this finalization.  The frozen NNUE model is validation-only and
is intentionally not committed.  Do not commit build directories, run output,
models, generated reports, or Windows artifacts.

The finalization stops before deployment: do not merge to `main`, deploy,
restart PM2, or touch `/home/ubuntu/hebichess-data/games.json` as part of this
phase.
