const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

const [scalarArtifact, simdArtifact, networkPath] = process.argv.slice(2);
if (!scalarArtifact || !simdArtifact || !networkPath)
  throw new Error('usage: node stockfish14_nnue_wasm_reference.cjs <scalar.js> <simd128.js> <network.nnue>');

const fen = 'r1bqkb1r/1p3ppp/p2ppn2/2n5/2BNP2P/2N1BQ2/PPP2PP1/R3K2R b KQkq - 2 9';
const networkBytes = fs.readFileSync(networkPath);

async function verify(artifact) {
  const factoryExport = require(path.resolve(artifact));
  const factory = typeof factoryExport === 'function' ? factoryExport : factoryExport.default;
  const module = await factory({ locateFile: file => path.join(path.dirname(path.resolve(artifact)), file) });
  module.ccall('hebichess_initialize', null, [], []);
  const linearMemoryAfterInit = module.HEAPU8.buffer.byteLength;

  const load = bytes => {
    const ptr = module._malloc(bytes.length);
    try {
      module.HEAPU8.set(bytes, ptr);
      return module.ccall('hebichess_stockfish14_nnue_load_bytes', 'number',
        ['number', 'number'], [ptr, bytes.length]) === 1;
    } finally { module._free(ptr); }
  };

  const invalidRejected = !load(networkBytes.subarray(0, 6));
  if (!invalidRejected) throw new Error(`${artifact}: truncated network was accepted`);
  const loadStarted = performance.now();
  if (!load(networkBytes)) throw new Error(`${artifact}: valid SF14 network was rejected`);
  const loadMs = performance.now() - loadStarted;
  const raw = module.ccall('hebichess_stockfish14_nnue_evaluate_fen', 'number', ['string'], [fen]);
  const repeatedRaw = module.ccall('hebichess_stockfish14_nnue_evaluate_fen', 'number', ['string'], [fen]);
  if (raw !== -12 || repeatedRaw !== raw)
    throw new Error(`${artifact}: raw eval parity failed (${raw}, ${repeatedRaw})`);
  return {
    artifact: path.resolve(artifact),
    invalid_load_rejected: invalidRejected,
    load_ms: loadMs,
    raw_eval_cp: raw,
    repeated_eval_deterministic: true,
    wasm_linear_memory_after_init_bytes: linearMemoryAfterInit,
    wasm_linear_memory_after_load_bytes: module.HEAPU8.buffer.byteLength,
    node_rss_bytes: process.memoryUsage().rss,
  };
}

(async () => {
  const [scalar, simd128] = await Promise.all([verify(scalarArtifact), verify(simdArtifact)]);
  if (scalar.raw_eval_cp !== simd128.raw_eval_cp)
    throw new Error('scalar and SIMD128 raw evaluations differ');
  console.log(JSON.stringify({ network_bytes: networkBytes.length, scalar, simd128,
    scalar_simd128_parity: scalar.raw_eval_cp === simd128.raw_eval_cp }, null, 2));
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
