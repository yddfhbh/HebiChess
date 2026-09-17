#!/usr/bin/env node
'use strict';

// Test-only Node runner for the three separately compiled WASM layouts.
// Example:
// node scripts/benchmark-wasm-nnue-layouts.js MODEL FENS \
//   build-wasm/nnue-layouts/hebichess-nnue-Original4.js \
//   build-wasm/nnue-layouts/hebichess-nnue-Prepacked4.js \
//   build-wasm/nnue-layouts/hebichess-nnue-Prepacked8.js 100

const fs = require('node:fs');
const path = require('node:path');

const [modelPath, positionsPath, ...rest] = process.argv.slice(2);
if (!modelPath || !positionsPath || rest.length < 3 || rest.length > 4) {
  throw Error('usage: benchmark-wasm-nnue-layouts.js MODEL FENS ORIGINAL4_JS PREPACKED4_JS PREPACKED8_JS [iterations]');
}
const [original4Path, prepacked4Path, prepacked8Path, iterationsText] = rest;
const iterations = iterationsText === undefined ? 100 : Number(iterationsText);
if (!Number.isInteger(iterations) || iterations <= 0) throw Error('iterations must be a positive integer');
const bytes = fs.readFileSync(modelPath);
const positions = fs.readFileSync(positionsPath, 'utf8').split(/\r?\n/)
  .filter(line => line && !line.startsWith('#'));
if (!positions.length) throw Error('position fixture is empty');

async function loadModule(modulePath) {
  const factory = require(path.resolve(modulePath));
  return factory({locateFile: file => path.join(path.dirname(path.resolve(modulePath)), file)});
}

async function benchmark(name, modulePath) {
  const module = await loadModule(modulePath);
  module.ccall('hebichess_initialize', 'number', [], []);
  const pointer = module._malloc(bytes.length);
  if (!pointer) throw Error(`${name}: model allocation failed`);
  try {
    module.HEAPU8.set(bytes, pointer);
    if (module.ccall('hebichess_nnue_load_bytes', 'number', ['number', 'number'], [pointer, bytes.length]) !== 1) {
      throw Error(`${name}: ${module.ccall('hebichess_take_output', 'string', [], [])}`);
    }
  } finally {
    module._free(pointer);
  }
  let checksum = 0;
  for (const fen of positions) checksum += module.ccall('hebichess_nnue_evaluate_fen_raw', 'number', ['string'], [fen]);
  const started = process.hrtime.bigint();
  for (let repeat = 0; repeat < iterations; ++repeat) {
    for (const fen of positions) checksum += module.ccall('hebichess_nnue_evaluate_fen_raw', 'number', ['string'], [fen]);
  }
  const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
  return {name, evaluations: positions.length * iterations, elapsed_ms: elapsedMs,
    evaluations_per_second: positions.length * iterations * 1000 / elapsedMs, checksum};
}

(async () => {
  const results = [];
  results.push(await benchmark('Original4', original4Path));
  results.push(await benchmark('Prepacked4', prepacked4Path));
  results.push(await benchmark('Prepacked8', prepacked8Path));
  const original = results[0];
  for (const result of results) result.speedup_vs_original_pct = (result.evaluations_per_second / original.evaluations_per_second - 1) * 100;
  // A checksum mismatch is a semantic failure, rather than a benchmark datum.
  if (results.some(result => result.checksum !== original.checksum)) throw Error('WASM layout checksum mismatch');
  console.log(JSON.stringify({network: modelPath, positions: positions.length, iterations, results}, null, 2));
})().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
