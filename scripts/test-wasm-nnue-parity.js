#!/usr/bin/env node
/* Actual Node-loaded WASM raw-NNUE parity against Python reference JSON. */
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const value = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };
const networkPath = path.resolve(root, value('--network') || '');
const referencesPath = path.resolve(root, value('--references') || 'tests/data/nnue-export-parity-100.json');
const wasmPath = path.resolve(root, value('--wasm') || process.env.HEBICHESS_WASM_NODE || 'build-wasm/node-test/hebichess-node.js');
const expectedSha256 = (value('--expected-network-sha256') || '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826').toLowerCase();

if (!value('--network')) throw Error('--network is required (the frozen .hebinnue is intentionally not tracked)');

const bytes = fs.readFileSync(networkPath);
const actualSha256 = crypto.createHash('sha256').update(bytes).digest('hex');
if (actualSha256 !== expectedSha256) {
  throw Error(`network SHA256 mismatch: expected ${expectedSha256}, got ${actualSha256}`);
}

let references;
try {
  references = JSON.parse(fs.readFileSync(referencesPath, 'utf8'));
} catch (error) {
  if (error.code === 'ENOENT') {
    throw Error(`missing Python reference JSON: ${referencesPath}\n` +
      'Generate it from the frozen model with: py -3 -m training.nnue.write_wasm_parity_reference ' +
      `--network ${value('--network')}`);
  }
  throw error;
}
if (references.network_sha256?.toLowerCase() !== actualSha256 ||
    !Array.isArray(references.samples) || references.samples.length !== 100) {
  throw Error('reference JSON must contain 100 scores for this exact network');
}
if (!references.samples.every(sample => typeof sample?.fen === 'string' &&
    Number.isFinite(Number(sample.raw_cp)))) {
  throw Error('reference JSON samples must each contain a FEN and finite Python raw_cp');
}

const loaded = require(wasmPath);
const factory = typeof loaded === 'function' ? loaded : loaded.default || loaded.createHebiChessNodeTestModule;
if (typeof factory !== 'function') throw Error('Node WASM artifact did not export a module factory');

function output(module) {
  return module.ccall('hebichess_take_output', 'string', [], []).trim();
}

function loadNetwork(module, input) {
  const pointer = module._malloc(input.length);
  try {
    module.HEAPU8.set(input, pointer);
    return module.ccall('hebichess_nnue_load_bytes', 'number', ['number', 'number'], [pointer, input.length]);
  } finally {
    module._free(pointer);
  }
}

function mustReject(module, label, input, fragment) {
  if (loadNetwork(module, input) !== 0 || !output(module).includes(fragment)) {
    throw Error(`${label} was not rejected with ${fragment}`);
  }
}

function normalizeFen(fen) {
  const fields = String(fen).trim().split(/\s+/);
  if (fields.length === 4) return `${fields.join(' ')} 0 1`;
  if (fields.length === 6) return fields.join(' ');
  throw Error(`FEN must have 4 canonical fields or 6 UCI fields, found ${fields.length}`);
}

function nonFiniteEvaluationError({index, fen, normalizedFen, rawWasmReturn, expected}) {
  return Error([
    `non-finite NNUE parity value at sample ${index}`,
    `original fixture FEN: ${fen}`,
    `normalized FEN: ${normalizedFen}`,
    `raw WASM return value: ${String(rawWasmReturn)}`,
    `expected Python value: ${String(expected)}`
  ].join('\n'));
}

function evaluateRecord(module, {fen, raw_cp: expected}, index) {
  let normalizedFen;
  try {
    normalizedFen = normalizeFen(fen);
  } catch (error) {
    throw nonFiniteEvaluationError({
      index,
      fen,
      normalizedFen: '(not applicable)',
      rawWasmReturn: `normalization error: ${error.message}`,
      expected
    });
  }
  // The C ABI returns const char*.  Emscripten's 'string' return converter is
  // therefore intentional; Number() is applied only after preserving it.
  const rawWasmReturn = module.ccall(
    'hebichess_nnue_evaluate_fen_raw', 'string', ['string'], [normalizedFen]
  );
  const actual = Number(rawWasmReturn);
  const numericExpected = Number(expected);
  if (!Number.isFinite(actual) || !Number.isFinite(numericExpected)) {
    throw nonFiniteEvaluationError({index, fen, normalizedFen, rawWasmReturn, expected});
  }
  const difference = Math.abs(actual - numericExpected);
  if (!Number.isFinite(difference)) {
    throw nonFiniteEvaluationError({index, fen, normalizedFen, rawWasmReturn, expected});
  }
  return {fen, normalizedFen, expected: numericExpected, actual, difference};
}

(async () => {
  const module = await factory({locateFile: file => path.join(path.dirname(wasmPath), file)});
  module.ccall('hebichess_initialize', null, [], []);
  if (!loadNetwork(module, bytes)) throw Error(output(module));

  const badActivation = Buffer.from(bytes);
  badActivation.writeUInt32LE(999, 44);
  mustReject(module, 'invalid v3 activation', badActivation, 'unsupported final hidden activation');
  const corrupt = Buffer.from(bytes);
  corrupt[corrupt.length - 1] ^= 1;
  mustReject(module, 'payload checksum corruption', corrupt, 'checksum mismatch');
  mustReject(module, 'truncated model', bytes.subarray(0, bytes.length - 1), 'truncated (parameters)');

  const records = references.samples.map((sample, index) => evaluateRecord(module, sample, index));
  const differences = records.map(record => record.difference).sort((a, b) => a - b);
  const worst = records.reduce((left, right) => left.difference >= right.difference ? left : right);
  const mean = differences.reduce((sum, difference) => sum + difference, 0) / differences.length;
  const median = (differences[49] + differences[50]) / 2;
  if (![worst.difference, mean, median].every(Number.isFinite)) {
    throw Error('NNUE parity summary contains a non-finite value');
  }

  const summary = {
    samples: records.length,
    max_abs_cp_diff: worst.difference,
    mean_abs_cp_diff: mean,
    median_abs_cp_diff: median,
    worst_fen: worst.fen,
    threshold: 'max_abs_cp_diff < 0.001',
    parser_rejections: 'PASS'
  };
  console.log(JSON.stringify(summary, null, 2));
  if (!(worst.difference < 0.001)) process.exitCode = 1;
})().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
