#!/usr/bin/env node
/*
 * Compare the two dedicated Node-compatible Emscripten H1 artifacts.  The
 * only intended code-generation difference is HEBICHESS_NNUE_HIDDEN1_VARIANT
 * (4 versus 8); all corpus parsing and comparison happens outside timing.
 */
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const option = name => {
  const index = args.indexOf(name);
  return index < 0 ? null : args[index + 1];
};
const positive = (name, fallback) => {
  const value = option(name);
  const parsed = Number(value === null ? fallback : value);
  if (!Number.isInteger(parsed) || parsed <= 0) throw Error(`${name} must be a positive integer`);
  return parsed;
};
const verboseProgress = args.includes('--verbose-progress');
const onlyPosition = option('--only');
const order = option('--order') || 'h1-4-first';
if (!['h1-4-first', 'h1-8-first'].includes(order)) {
  throw Error('--order must be h1-4-first or h1-8-first');
}
const progress = message => {
  if (verboseProgress) console.error(`[progress] ${message}`);
};

const networkPath = option('--network') && path.resolve(root, option('--network'));
const h1_4_path = path.resolve(root, option('--h1-4') || 'build-wasm/h1-ab/h1-4/hebichess.js');
const h1_8_path = path.resolve(root, option('--h1-8') || 'build-wasm/h1-ab/h1-8/hebichess.js');
const corpusPath = path.resolve(root, option('--corpus') || 'tests/data/wasm-parity-100.fen');
const fixturePath = path.resolve(root, option('--fixture') || 'tests/data/phase6-search-baseline.fen');
const outputPath = path.resolve(root, option('--output') || 'runs/wasm-h1-ab.json');
const expectedNetworkSha256 = (option('--expected-network-sha256') ||
  '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826').toLowerCase();
const depth = positive('--depth', 5);
const timeMs = positive('--time-ms', 1000);
const evalRepeats = positive('--eval-repeats', 32);

if (!networkPath) throw Error('--network is required (the frozen model is intentionally not tracked)');

function readFenCorpus(file) {
  const fens = fs.readFileSync(file, 'utf8').split(/\r?\n/).filter(line => line && !line.startsWith('#'));
  if (fens.length !== 100 || new Set(fens).size !== 100) throw Error(`${file} must contain 100 unique FENs`);
  return fens;
}

function readSearchFixture(file) {
  const rows = fs.readFileSync(file, 'utf8').split(/\r?\n/)
    .filter(line => line && !line.startsWith('#')).map(line => {
      const tab = line.indexOf('\t');
      if (tab <= 0 || tab === line.length - 1) throw Error(`invalid search fixture row: ${line}`);
      return { name: line.slice(0, tab), fen: line.slice(tab + 1) };
    });
  if (rows.length !== 10) throw Error(`${file} must contain the canonical 10 search FENs`);
  return rows;
}

function factoryFor(artifact) {
  if (!fs.existsSync(artifact)) throw Error(`missing Node WASM artifact: ${artifact}`);
  const loaded = require(artifact);
  const factory = typeof loaded === 'function' ? loaded :
    loaded.default || loaded.createHebiChessWasmH1BenchmarkModule;
  if (typeof factory !== 'function') throw Error(`WASM factory missing from ${artifact}`);
  return factory;
}

async function createModule(artifact) {
  const module = await factoryFor(artifact)({ locateFile: file => path.join(path.dirname(artifact), file) });
  if (typeof module.ccall !== 'function' || !module.HEAPU8) throw Error(`incomplete Node WASM ABI: ${artifact}`);
  module.ccall('hebichess_initialize', null, [], []);
  return module;
}

function takeOutput(module) {
  return module.ccall('hebichess_take_output', 'string', [], []) || '';
}

function command(module, text) {
  module.ccall('hebichess_send_command', null, ['string'], [text]);
  return takeOutput(module).split(/\r?\n/).filter(Boolean);
}

function loadNetwork(module, bytes) {
  const pointer = module._malloc(bytes.length);
  try {
    module.HEAPU8.set(bytes, pointer);
    return module.ccall('hebichess_nnue_load_bytes', 'number', ['number', 'number'], [pointer, bytes.length]);
  } finally {
    module._free(pointer);
  }
}

function checkHardFailures(module, bytes) {
  const cases = [
    ['invalid v3 activation', (() => { const copy = Buffer.from(bytes); copy.writeUInt32LE(999, 44); return copy; })(),
      'unsupported final hidden activation'],
    ['payload checksum corruption', (() => { const copy = Buffer.from(bytes); copy[copy.length - 1] ^= 1; return copy; })(),
      'checksum mismatch'],
    ['truncated model', bytes.subarray(0, bytes.length - 1), 'truncated (parameters)']
  ];
  for (const [label, input, expected] of cases) {
    const accepted = loadNetwork(module, input);
    const text = takeOutput(module);
    if (accepted !== 0 || !text.includes(expected)) {
      throw Error(`${label} did not hard-fail with '${expected}': ${text}`);
    }
  }
  if (!loadNetwork(module, bytes)) throw Error(`frozen network reload failed: ${takeOutput(module)}`);
}

function rawEvaluate(module, fen, index) {
  const rawText = module.ccall('hebichess_nnue_evaluate_fen_raw', 'string', ['string'], [fen]);
  const rawCp = Number(rawText);
  if (!Number.isFinite(rawCp)) throw Error(`non-finite raw NNUE score at corpus index ${index}: ${rawText}`);
  return { rawText, rawCp };
}

function roundedCp(value) {
  return value < 0 ? -Math.round(-value) : Math.round(value);
}

function parseField(line, field) {
  const match = line.match(new RegExp(`\\b${field} (-?\\d+)\\b`));
  return match ? Number(match[1]) : null;
}

function parseSearch(lines) {
  const errors = lines.filter(line => line.startsWith('info string error '));
  const bestmoves = lines.filter(line => line.startsWith('bestmove '));
  const info = lines.map(line => line.match(/^info depth (\d+) score (cp|mate) (-?\d+) nodes (\d+) qnodes (\d+)$/)).filter(Boolean);
  const final = info.at(-1);
  const tm = lines.find(line => line.startsWith('info string tm ')) || '';
  const stats = lines.find(line => line.startsWith('info string nodes ')) || '';
  const qprofile = lines.find(line => line.startsWith('info string qprofile ')) || '';
  const qMaxPly = parseField(qprofile, 'max_qply');
  return {
    bestmove: bestmoves.length === 1 ? bestmoves[0].split(/\s+/)[1] : null,
    bestmove_count: bestmoves.length,
    score: final ? `${final[2]} ${final[3]}` : null,
    completed_depth: final ? Number(final[1]) : 0,
    nodes: parseField(stats, 'nodes') ?? (final ? Number(final[4]) : 0),
    qnodes: parseField(stats, 'qnodes') ?? (final ? Number(final[5]) : 0),
    nps: parseField(tm, 'nps'),
    elapsed_ms: parseField(tm, 'elapsed'),
    // Extract existing engine telemetry outside WASM so production search
    // timing does not gain any instrumentation cost.
    q_max_ply: qMaxPly,
    telemetry_ok: qMaxPly !== null,
    errors,
    protocol_ok: bestmoves.length === 1 && Boolean(final) && errors.length === 0
  };
}

function maxQMaxPly(records, variant) {
  return records.reduce((maximum, row) => {
    const value = row[variant].q_max_ply;
    return value === null ? maximum : Math.max(maximum, value);
  }, 0);
}

function runSearch(module, variant, position, kind, go) {
  progress(`${kind} ${variant} ${position.name}: start`);
  command(module, 'ucinewgame');
  command(module, `position fen ${position.fen}`);
  const started = performance.now();
  const result = parseSearch(command(module, go));
  result.wall_elapsed_ms = performance.now() - started;
  if (result.nps === null && result.wall_elapsed_ms > 0)
    result.nps = Math.round(result.nodes * 1000 / result.wall_elapsed_ms);
  progress(`${kind} ${variant} ${position.name}: done (${result.wall_elapsed_ms.toFixed(1)} ms, ` +
    `bestmove_count=${result.bestmove_count}, depth=${result.completed_depth})`);
  return result;
}

function runSearchPair(position, kind, go, h1_4, h1_8) {
  // Variant labels stay attached to their actual artifact regardless of run
  // order, so reverse-order measurements cannot invert report semantics.
  if (order === 'h1-4-first') {
    return {
      h1_4: runSearch(h1_4, 'h1_4', position, kind, go),
      h1_8: runSearch(h1_8, 'h1_8', position, kind, go)
    };
  }
  return {
    h1_8: runSearch(h1_8, 'h1_8', position, kind, go),
    h1_4: runSearch(h1_4, 'h1_4', position, kind, go)
  };
}

function pairRows(positions, left, right, kind) {
  const records = positions.map((position, index) => ({
    name: position.name,
    fen: position.fen,
    h1_4: left[index],
    h1_8: right[index]
  }));
  const mismatches = records.filter(row => !row.h1_4.protocol_ok || !row.h1_8.protocol_ok ||
    row.h1_4.bestmove !== row.h1_8.bestmove || row.h1_4.score !== row.h1_8.score ||
    (kind === 'fixed_depth' && row.h1_4.completed_depth !== row.h1_8.completed_depth));
  return { records, mismatches: mismatches.map(row => row.name) };
}

async function main() {
  const network = fs.readFileSync(networkPath);
  const networkSha256 = crypto.createHash('sha256').update(network).digest('hex');
  if (networkSha256 !== expectedNetworkSha256) {
    throw Error(`network SHA256 mismatch: expected ${expectedNetworkSha256}, got ${networkSha256}`);
  }
  const corpus = readFenCorpus(corpusPath);
  const fixturePositions = readSearchFixture(fixturePath);
  const positions = onlyPosition ? fixturePositions.filter(position => position.name === onlyPosition) : fixturePositions;
  if (positions.length === 0) {
    throw Error(`--only '${onlyPosition}' does not match a position in ${fixturePath}`);
  }
  progress(`selected ${positions.length}/${fixturePositions.length} search position(s); order=${order}`);
  const h1_4 = await createModule(h1_4_path);
  const h1_8 = await createModule(h1_8_path);
  for (const [label, module] of [['h1_4', h1_4], ['h1_8', h1_8]]) {
    if (!loadNetwork(module, network)) throw Error(`${label} could not load frozen network: ${takeOutput(module)}`);
    checkHardFailures(module, network);
    const setting = command(module, 'setoption name EvalMode value NNUE').join('\n');
    if (!setting.includes('info string EvalMode NNUE')) throw Error(`${label} did not enter NNUE mode: ${setting}`);
  }

  const raw4 = corpus.map((fen, index) => rawEvaluate(h1_4, fen, index));
  const raw8 = corpus.map((fen, index) => rawEvaluate(h1_8, fen, index));
  const rawMismatches = corpus.flatMap((fen, index) => {
    const h1_4_value = raw4[index], h1_8_value = raw8[index];
    return h1_4_value.rawText === h1_8_value.rawText ? [] : [{
        index, fen, h1_4_raw_cp: h1_4_value.rawCp, h1_8_raw_cp: h1_8_value.rawCp,
        h1_4_rounded_cp: roundedCp(h1_4_value.rawCp), h1_8_rounded_cp: roundedCp(h1_8_value.rawCp)
      }];
  });
  const roundedMismatches = rawMismatches.filter(item => item.h1_4_rounded_cp !== item.h1_8_rounded_cp);

  const benchmarkFens = fs.readFileSync(corpusPath, 'utf8');
  const runMicrobench = (module, variant) => {
    progress(`evaluator_microbench ${variant}: start`);
    const result = JSON.parse(module.ccall('hebichess_nnue_benchmark_fens', 'string', ['string', 'number'], [
      benchmarkFens, evalRepeats
    ]));
    progress(`evaluator_microbench ${variant}: done`);
    return result;
  };
  const micro = order === 'h1-4-first'
    ? { h1_4: runMicrobench(h1_4, 'h1_4'), h1_8: runMicrobench(h1_8, 'h1_8') }
    : { h1_8: runMicrobench(h1_8, 'h1_8'), h1_4: runMicrobench(h1_4, 'h1_4') };
  const { h1_4: micro4, h1_8: micro8 } = micro;
  if (!micro4.ok || !micro8.ok) throw Error(`WASM evaluator microbench failed: ${JSON.stringify({ micro4, micro8 })}`);

  const fixedDepthPairs = positions.map(position =>
    runSearchPair(position, 'fixed_depth', `go depth ${depth}`, h1_4, h1_8));
  const fixedDepth = pairRows(positions, fixedDepthPairs.map(pair => pair.h1_4),
    fixedDepthPairs.map(pair => pair.h1_8), 'fixed_depth');
  const fixedTimePairs = positions.map(position =>
    runSearchPair(position, 'fixed_time', `go movetime ${timeMs}`, h1_4, h1_8));
  const fixedTime = pairRows(positions, fixedTimePairs.map(pair => pair.h1_4),
    fixedTimePairs.map(pair => pair.h1_8), 'fixed_time');
  const fixedTimeProtocolFailures = fixedTime.records.filter(row =>
    !row.h1_4.protocol_ok || !row.h1_8.protocol_ok).map(row => row.name);
  const qMaxPlyTelemetryFailures = [...fixedDepth.records, ...fixedTime.records]
    .filter(row => !row.h1_4.telemetry_ok || !row.h1_8.telemetry_ok)
    .map(row => row.name);
  const qMaxPly = {
    max_search_ply_limit: 128,
    fixed_depth: {
      h1_4: maxQMaxPly(fixedDepth.records, 'h1_4'),
      h1_8: maxQMaxPly(fixedDepth.records, 'h1_8')
    },
    fixed_time: {
      h1_4: maxQMaxPly(fixedTime.records, 'h1_4'),
      h1_8: maxQMaxPly(fixedTime.records, 'h1_8')
    }
  };
  qMaxPly.overall = {
    h1_4: Math.max(qMaxPly.fixed_depth.h1_4, qMaxPly.fixed_time.h1_4),
    h1_8: Math.max(qMaxPly.fixed_depth.h1_8, qMaxPly.fixed_time.h1_8)
  };

  const report = {
    schema: 'hebichess-wasm-h1-ab-v1',
    // Fixed-time work intentionally does not assert equal nodes or completed
    // depth; it is the performance sample.  Its move/score differences remain
    // visible in the report, while fixed-depth parity (and valid UCI output
    // from both runs) is the correctness gate.
    status: rawMismatches.length === 0 && roundedMismatches.length === 0 &&
      fixedDepth.mismatches.length === 0 && fixedTimeProtocolFailures.length === 0 &&
      qMaxPlyTelemetryFailures.length === 0 ? 'PASS' : 'FAIL',
    configuration: {
      network: networkPath, network_sha256: networkSha256, corpus: corpusPath, search_fixture: fixturePath,
      depth, fixed_time_ms: timeMs, evaluator_repeats: evalRepeats,
      selected_search_positions: positions.map(position => position.name), order,
      targets: { h1_4: h1_4_path, h1_8: h1_8_path },
      h1_4_artifact_sha256: crypto.createHash('sha256').update(fs.readFileSync(h1_4_path)).digest('hex'),
      h1_8_artifact_sha256: crypto.createHash('sha256').update(fs.readFileSync(h1_8_path)).digest('hex')
    },
    correctness: {
      nnue_hard_fail_behavior: 'PASS (activation, checksum, truncated model; both variants)',
      raw_nnue_parity: { positions: corpus.length, exact_raw_mismatch_count: rawMismatches.length,
        rounded_cp_mismatch_count: roundedMismatches.length, mismatches: rawMismatches }
    },
    evaluator_microbench: { h1_4: micro4, h1_8: micro8 },
    depth_safety: { q_max_ply: qMaxPly, telemetry_failures: qMaxPlyTelemetryFailures },
    search: { fixed_depth: fixedDepth, fixed_time: { ...fixedTime, protocol_failures: fixedTimeProtocolFailures } }
  };
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ status: report.status, output: outputPath, correctness: report.correctness,
    evaluator_microbench: report.evaluator_microbench,
    depth_safety: report.depth_safety,
    fixed_depth_mismatches: fixedDepth.mismatches, fixed_time_mismatches: fixedTime.mismatches }, null, 2));
  if (report.status !== 'PASS') process.exitCode = 1;
}

main().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
