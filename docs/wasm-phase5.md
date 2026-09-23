# Phase 5: browser WASM NNUE

The browser engine is still browser-authoritative: the Worker owns the C++
engine and `GameState`; the Node process only relays the browser-produced game
snapshot.  HCE remains the application default.  Supplying `?eval=nnue` to
the game page is an explicit opt-in; it never presents an HCE search as NNUE.

## Model loading and memory

`web/public/engine/hebichess-worker.js` fetches the content-addressed asset
`models/hebinnue-v3-4c815d54bc6c9fbf.hebinnue` inside the Worker.  It verifies
the candidate SHA-256 (`4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826`),
copies the bytes into a temporary WASM allocation, then calls
`hebichess_nnue_load_bytes`.  The C++ byte loader is also used by native file
loading; it alone validates the generic v1/v2/v3 headers, dimensions, ABI,
payload size, FNV checksum, and v3 activation.  Unknown versions and
activation enums fail closed.

The C++ parser copies tensor slices directly into the resident `Network`; it
does not make an additional full `vector<float>` payload copy.  At steady
state the v3 128/128 network consumes about 50.7 MB in WASM memory.  During
load there is also one ~50.7 MB WASM transfer allocation and one fetched JS
`ArrayBuffer`; both are released after parsing.  Browser WASM starts with
192 MiB, can grow to 384 MiB, and needs the normal engine/search allocations
in addition to the model.

`hce-ready`, `nnue-loading`, `nnue-ready`, and `nnue-load-failed` are explicit
Worker/client states.  Selecting NNUE before readiness rejects; `go` also
rejects if an NNUE search somehow reaches it while not ready.  Browser
`EvalFile` host paths are deliberately rejected: use
`loadNnue({url, sha256})`, then `setEvalMode('NNUE')`.

There is no service worker.  The Node static server sends a one-year immutable
cache header only for the SHA-named `.hebinnue` file.  First NNUE use downloads
about 50.7 MB; later visits reuse the browser HTTP cache until the asset name
changes.

## Windows/local verification

Do not add the model to Git.  Copy this input to the same relative path on
Windows before testing:

- `runs/full-phase4-finalrelu-h128-128-lr1e-4/best_balanced.hebinnue`

`tests/data/wasm-parity-100.fen` is the tracked canonical 100-FEN corpus, so
it is already present in a fresh checkout. Its `positions_sha256` is the
canonical UTF-8 LF-normalized corpus SHA256 (not the checkout's raw file-byte
SHA256), which keeps verification identical on Windows CRLF checkouts. The
reference JSON is generated locally from the frozen model and is intentionally
not tracked.

From the repository root in an Emscripten-enabled Developer Command Prompt:

```bat
certutil -hashfile runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue SHA256
rem Raw corpus bytes can differ after Windows CRLF checkout; the generator verifies canonical UTF-8 LF-normalized SHA256.
mkdir web\public\engine\models
copy /Y runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue web\public\engine\models\hebinnue-v3-4c815d54bc6c9fbf.hebinnue
py -3 -m training.nnue.write_wasm_parity_reference --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue --positions tests\data\wasm-parity-100.fen --output tests\data\nnue-export-parity-100.json
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
call %EMSDK%\emsdk_env.bat
emcmake cmake -S . -B build-wasm -DHEBICHESS_BUILD_WASM=ON -DHEBICHESS_BUILD_WASM_NODE_TEST=ON
cmake --build build-wasm --target HebiChessWasm HebiChessWasmNodeTest
node scripts\test-wasm-nnue-parity.js --network runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue
cd web
npm test
node server.js
```

Confirm the model SHA-256 from the task before continuing; the generator
independently verifies the corpus's canonical UTF-8 LF-normalized SHA256.
With the server running, use Chrome at these local URLs:

- `http://127.0.0.1:3400/engine-test` — existing HCE smoke.
- `http://127.0.0.1:3400/public/wasm-nnue-parity.html` — 100 raw scores;
  requires `max_abs_cp_diff < 0.001`.
- `http://127.0.0.1:3400/public/engine-nnue-smoke.html` — startpos, tactical,
  and historical Phase 4 index 9 NNUE searches, including repeat determinism.
- `http://127.0.0.1:3400/?eval=nnue` — explicit browser-game NNUE path.

For deployment later, publish the WASM artifacts using the existing artifact
workflow and publish the model as the SHA-named static file above.  Verify the
asset SHA at publish time, retain HCE as default, and deploy neither as part of
this Phase 5 implementation.
