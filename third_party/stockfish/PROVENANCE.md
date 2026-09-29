# Stockfish NNUE experiment provenance

This isolated experimental backend vendors the NNUE implementation from
official Stockfish tag `sf_19`, commit
[`edb0d9db6731067ec50ce619ff372b463bc4dd5d`](https://github.com/official-stockfish/Stockfish/commit/edb0d9db6731067ec50ce619ff372b463bc4dd5d).
The compatible default network specified by that source is
[`nn-1a298aa575a0.nnue`](https://github.com/official-stockfish/networks/blob/master/nn-1a298aa575a0.nnue).
Its downloaded size is 98,511,183 bytes and its full
SHA-256 is
`1A298AA575A085434D29027978DC36867FE9C5BCEA9376654B7A8EBA1E52DFC2`.
The 12-character filename digest follows the official Stockfish networks
repository naming convention. The network repository states that its network
authors uploaded the files under CC0:
https://github.com/official-stockfish/networks#networks

Stockfish 19's [`evaluate.h`](https://github.com/official-stockfish/Stockfish/blob/sf_19/src/evaluate.h)
selects this default network, and [`nnue_architecture.h`](https://github.com/official-stockfish/Stockfish/blob/sf_19/src/nnue/nnue_architecture.h)
binds it to `FullThreats`,
`PP_3Wide`, and `HalfKAv2_hm`. The network parser, feature definitions,
accumulator updates, integer quantization, activation, and network layers are
upstream code; they are not reimplemented by the HebiChess adapter.

## Vendored source scope

The upstream `src/nnue/` subtree is copied intact, along with `src/incbin/`
and the Stockfish core dependencies needed to construct a `Position`, generate
its features, and update its accumulator: `attacks.{h,cpp}`, `bitboard.{h,cpp}`,
`engine.h`, `history.h`, `memory.h`, `misc.{h,cpp}`, `movegen.{h,cpp}`,
`numa.h`, `position.{h,cpp}`, `score.h`, `search.h`, `shm.h`, `thread.h`,
`thread_native.h`, `timeman.h`, `tune.h`, `tt.h`, `types.h`, `uci.h`, `ucioption.h`,
and `syzygy/tbprobe.h`. These core headers are required by the upstream
position/NNUE interfaces. No Stockfish search, move ordering, or transposition
table implementation is compiled or linked.

The adapter is `src/chess/stockfish_nnue.{hpp,cpp}`. Its only representation
bridge currently creates an upstream `Position` from HebiChess's FEN, then
calls the upstream network/accumulator APIs. The experimental build macro
`HEBICHESS_STOCKFISH_NNUE_ONLY` excludes Stockfish-only TT prefetch and
position-print/tablebase diagnostics from `position.cpp` so the evaluator does
not pull in Stockfish TT/UCI/tablebase implementations. FEN en-passant square
formatting is expressed locally instead of depending on `UCIEngine::square`.

The only NNUE API delta is an added `Network::load_from_memory` entry point
that feeds the same upstream stream parser, allowing the WASM host to supply
network bytes without filesystem access. Upstream feature/network computation
is otherwise unchanged. This local delta and the compile-time exclusions are
the only intended modifications within the vendored source tree.

## Licensing

Stockfish source files are GPLv3-or-later and retain their upstream copyright
headers. `COPYING.txt` is the upstream GPLv3 license text. The upstream
Stockfish project identifies itself as GPLv3-or-later in its
[license notice](https://github.com/official-stockfish/Stockfish/blob/sf_19/Copying.txt).
The network above is separately documented as CC0 by its authors in the
official networks repository. HebiChess's repository root has no LICENSE or
COPYING file, so this checkout does not establish a repository license
compatibility conclusion. Distribution of this combined experimental binary
must be reviewed against GPLv3 obligations; this experiment does not attempt
to avoid those obligations.

## Validation note

The experimental MSVC AVX2 build currently disagrees with the upstream scalar
reference on the required regression FEN (`-411 cp` versus `-15 cp`). For that
reason AVX2 is opt-in and disabled by default in these experimental CMake
targets. Native scalar, WASM scalar, and WASM SIMD128 agree at `-15 cp`.
This AVX2 discrepancy is unresolved and blocks treating that native vector
configuration as validated.
