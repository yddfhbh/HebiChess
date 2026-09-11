# Phase 3 browser-authoritative state

The browser Worker owns a `GameState` backed by the same C++ core as search.
The bridge exposes reset, FEN load, legal UCI moves, apply-UCI (including SAN),
and terminal status (`ongoing`, `checkmate`, `stalemate`, `threefold repetition`,
`50-move rule`, and `insufficient material`). Node accepts only an owner,
`gameId`, exact `revision`, and a structured snapshot; it does not generate or
validate chess moves.

`web/benchmark.js [games] [plies]` runs a safe synthetic relay benchmark. Its
input snapshots represent work already completed by the browser and therefore
measure server relay/fanout overhead independently of engine search.
