# WASM Phase 1 verification

The canonical parity input is `tests/data/wasm-parity-100.fen`. It contains
100 unique legal, non-terminal positions generated from seed
`0x9e3779b97f4a7c15` using the deterministic xorshift64 legal-move sampler.

Build the test-only Node-compatible artifact and run the native/actual-WASM
comparison from the repository root:

```sh
source /home/ubuntu/.local/share/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DHEBICHESS_BUILD_WASM_NODE_TEST=ON
cmake --build build-wasm --target HebiChessWasmNodeTest
node scripts/test-wasm-parity.js --depth 4 --start 0 --count 25
```

Each invocation is one immediately-persisted JSON chunk under
`build-wasm/parity/`. Required chunks are depth 4 as `25 x 4`, depth 5 as
`10 x 5`, and depth 6 as `10 x 2`; use `--start` and `--count`. The runner
compares native Release and the actual Node-loaded WASM with the same
`ucinewgame`, `position fen`, and `go depth` commands. It records malformed
output, crashes, FEN, bestmove, score, and persistent-versus-fresh state checks.

The browser artifact remains `web/public/engine/hebichess.js` and
`web/public/engine/hebichess.wasm`, built only with the browser target:

```sh
emcmake cmake -S . -B build-wasm -DHEBICHESS_BUILD_WASM=ON
cmake --build build-wasm --target HebiChessWasm
```

The parity artifact is separate: `build-wasm/node-test/hebichess-node.js` and
its adjacent `.wasm` file. It uses the same C++ sources and
`-sENVIRONMENT=web,worker,node`; it is never served by the browser harness.

For external Chrome smoke testing, run a temporary localhost-only server with
an isolated data file:

```sh
mkdir -p /tmp/hebichess-phase1-data && printf '{"games":[]}\n' > /tmp/hebichess-phase1-data/games.json
PORT=3410 DATA_PATH=/tmp/hebichess-phase1-data/games.json NODE_ENV=development node web/server.js
```

Open `http://127.0.0.1:3410/engine-test` and use **Run Phase 1 Browser Tests**.
The **Copy JSON Results** button copies the final summary. Stop the temporary
server with Ctrl-C; do not register it with PM2.

The production game remains server-engine backed. No production deploy is
implied by these verification paths.
