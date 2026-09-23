#!/usr/bin/env node
/* Production-equivalent QSearch vector/fixed-buffer WASM acceptance runner. */
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const option = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };
const positive = (name, fallback) => {
  const n = Number(option(name) ?? fallback);
  if (!Number.isInteger(n) || n <= 0) throw Error(`${name} must be a positive integer`);
  return n;
};
const order = option('--order') || 'baseline-first';
if (!['baseline-first', 'candidate-first'].includes(order))
  throw Error('--order must be baseline-first or candidate-first');
const networkPath = option('--network') && path.resolve(root, option('--network'));
const baselinePath = path.resolve(root, option('--baseline') || 'build-wasm/move-buffer-ab/baseline/hebichess.js');
const candidatePath = path.resolve(root, option('--candidate') || 'build-wasm/move-buffer-ab/candidate/hebichess.js');
const fixturePath = path.resolve(root, option('--fixture') || 'tests/data/phase6-search-baseline.fen');
const outputPath = path.resolve(root, option('--output') || 'runs/wasm-qsearch-move-buffer-ab.json');
const expectedSha = (option('--expected-network-sha256') ||
  '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826').toLowerCase();
const depth = positive('--depth', 5), timeMs = positive('--time-ms', 1000);
const only = option('--only'), verbose = args.includes('--verbose-progress');
if (!networkPath) throw Error('--network is required');
const progress = text => { if (verbose) console.error(`[progress] ${text}`); };

function fixture(file) {
  const rows = fs.readFileSync(file, 'utf8').split(/\r?\n/).filter(x => x && !x.startsWith('#')).map(line => {
    const tab = line.indexOf('\t');
    if (tab <= 0 || tab === line.length - 1) throw Error(`invalid fixture row: ${line}`);
    return {name: line.slice(0, tab), fen: line.slice(tab + 1)};
  });
  if (rows.length !== 10) throw Error(`${file} must contain the canonical 10 search FENs`);
  return only ? rows.filter(row => row.name === only) : rows;
}
function factory(artifact) {
  if (!fs.existsSync(artifact)) throw Error(`missing Node WASM artifact: ${artifact}`);
  const loaded = require(artifact);
  const value = typeof loaded === 'function' ? loaded :
    loaded.default || loaded.createHebiChessWasmMoveBufferBenchmarkModule;
  if (typeof value !== 'function') throw Error(`WASM factory missing from ${artifact}`);
  return value;
}
async function moduleFor(artifact, network) {
  const module = await factory(artifact)({locateFile: file => path.join(path.dirname(artifact), file)});
  module.ccall('hebichess_initialize', null, [], []);
  const ptr = module._malloc(network.length);
  try {
    module.HEAPU8.set(network, ptr);
    if (!module.ccall('hebichess_nnue_load_bytes', 'number', ['number', 'number'], [ptr, network.length]))
      throw Error(`could not load frozen network into ${artifact}: ${take(module)}`);
  } finally { module._free(ptr); }
  const set = command(module, 'setoption name EvalMode value NNUE').join('\n');
  if (!set.includes('info string EvalMode NNUE')) throw Error(`NNUE mode rejected by ${artifact}`);
  return module;
}
const take = module => module.ccall('hebichess_take_output', 'string', [], []) || '';
const command = (module, text) => { module.ccall('hebichess_send_command', null, ['string'], [text]); return take(module).split(/\r?\n/).filter(Boolean); };
const numberField = (line, name) => {
  const found = line.match(new RegExp(`\\b${name} (\\d+)\\b`)); return found ? Number(found[1]) : null;
};
function parse(lines) {
  const info = lines.map(x => x.match(/^info depth (\d+) score (cp|mate) (-?\d+) nodes (\d+) qnodes (\d+)$/)).filter(Boolean).at(-1);
  const best = lines.filter(x => x.startsWith('bestmove '));
  const tm = lines.find(x => x.startsWith('info string tm ')) || '';
  const q = lines.find(x => x.startsWith('info string qprofile ')) || '';
  const value = name => numberField(q, name);
  return {
    bestmove: best.length === 1 ? best[0].split(/\s+/)[1] : null, bestmove_count: best.length,
    score: info ? `${info[2]} ${info[3]}` : null, completed_depth: info ? Number(info[1]) : 0,
    nodes: numberField(tm, 'nodes') ?? (info ? Number(info[4]) : 0),
    qnodes: numberField(tm, 'qnodes') ?? (info ? Number(info[5]) : 0),
    elapsed_ms: numberField(tm, 'elapsed'), nps: numberField(tm, 'nps'),
    q_max_ply: value('max_qply'), qsearch_total_time_us: value('qsearch_total_us'),
    move_generation_time_us: value('movegen_us'), move_ordering_time_us: value('ordering_us'),
    allocation_counters: {allocations: value('vector_allocations'), reallocations: value('vector_reallocations'), allocated_bytes: value('vector_allocated_bytes')},
    protocol_ok: best.length === 1 && Boolean(info) && !lines.some(x => x.startsWith('info string error '))
  };
}
function search(module, label, row, kind, go) {
  progress(`${kind} ${label} ${row.name}: start`);
  command(module, 'ucinewgame'); command(module, `position fen ${row.fen}`);
  const started = performance.now(), result = parse(command(module, go));
  result.wall_elapsed_ms = performance.now() - started;
  if (result.nps === null && result.wall_elapsed_ms > 0) result.nps = Math.round(result.nodes * 1000 / result.wall_elapsed_ms);
  progress(`${kind} ${label} ${row.name}: done (${result.wall_elapsed_ms.toFixed(1)} ms)`);
  return result;
}
function runPair(base, candidate, row, kind, go) {
  return order === 'baseline-first'
    ? {baseline: search(base, 'baseline', row, kind, go), candidate: search(candidate, 'candidate', row, kind, go)}
    : {candidate: search(candidate, 'candidate', row, kind, go), baseline: search(base, 'baseline', row, kind, go)};
}
function aggregate(records, key) {
  const baseNodes = records.reduce((sum, x) => sum + x.baseline.nodes, 0);
  const candidateNodes = records.reduce((sum, x) => sum + x.candidate.nodes, 0);
  const baseMs = records.reduce((sum, x) => sum + x.baseline.wall_elapsed_ms, 0);
  const candidateMs = records.reduce((sum, x) => sum + x.candidate.wall_elapsed_ms, 0);
  const rel = records.map(x => x.candidate.nps / x.baseline.nps).sort((a, b) => a - b);
  return {benchmark: key, baseline_nps: baseNodes * 1000 / baseMs, candidate_nps: candidateNodes * 1000 / candidateMs,
    median_relative_nps: rel[Math.floor(rel.length / 2)], baseline_wall_ms: baseMs, candidate_wall_ms: candidateMs};
}
async function main() {
  const network = fs.readFileSync(networkPath), sha = crypto.createHash('sha256').update(network).digest('hex');
  if (sha !== expectedSha) throw Error(`network SHA256 mismatch: expected ${expectedSha}, got ${sha}`);
  const rows = fixture(fixturePath); if (!rows.length) throw Error(`--only '${only}' did not match a canonical position`);
  const baseline = await moduleFor(baselinePath, network), candidate = await moduleFor(candidatePath, network);
  const layout = {baseline: JSON.parse(baseline.ccall('hebichess_qsearch_move_buffer_layout', 'string', [], [])),
    candidate: JSON.parse(candidate.ccall('hebichess_qsearch_move_buffer_layout', 'string', [], []))};
  const fixedDepth = rows.map(row => ({name: row.name, fen: row.fen, ...runPair(baseline, candidate, row, 'fixed_depth', `go depth ${depth}`)}));
  const fixedTime = rows.map(row => ({name: row.name, fen: row.fen, ...runPair(baseline, candidate, row, 'fixed_time', `go movetime ${timeMs}`)}));
  const depthMismatches = fixedDepth.filter(x => !x.baseline.protocol_ok || !x.candidate.protocol_ok ||
    x.baseline.bestmove !== x.candidate.bestmove || x.baseline.score !== x.candidate.score ||
    x.baseline.completed_depth !== x.candidate.completed_depth || x.baseline.nodes !== x.candidate.nodes || x.baseline.qnodes !== x.candidate.qnodes).map(x => x.name);
  const timeMismatches = fixedTime.filter(x => !x.baseline.protocol_ok || !x.candidate.protocol_ok ||
    x.baseline.bestmove !== x.candidate.bestmove || x.baseline.score !== x.candidate.score).map(x => x.name);
  const qMax = records => Math.max(...records.flatMap(x => [x.baseline.q_max_ply ?? 0, x.candidate.q_max_ply ?? 0]));
  const telemetryMissing = [...fixedDepth, ...fixedTime].filter(x => x.baseline.q_max_ply === null || x.candidate.q_max_ply === null).map(x => x.name);
  const report = {schema: 'hebichess-wasm-qsearch-move-buffer-ab-v1',
    status: depthMismatches.length === 0 && telemetryMissing.length === 0 ? 'PASS' : 'FAIL',
    configuration: {network: networkPath, network_sha256: sha, fixture: fixturePath, depth, fixed_time_ms: timeMs, order,
      stack_size_bytes: 2097152, targets: {baseline: baselinePath, candidate: candidatePath}, selected_search_positions: rows.map(x => x.name)},
    layout, stack_safety: {q_max_ply: {fixed_depth: qMax(fixedDepth), fixed_time: qMax(fixedTime)}, telemetry_missing: telemetryMissing,
      fixed_and_ordered_buffers_simultaneously_live: layout.candidate.fixed_and_ordered_buffers_simultaneously_live},
    fixed_depth_mismatches: depthMismatches, fixed_time_mismatches: timeMismatches,
    search: {fixed_depth: {records: fixedDepth, aggregate: aggregate(fixedDepth, 'fixed_depth')}, fixed_time: {records: fixedTime, aggregate: aggregate(fixedTime, 'fixed_time')}}};
  fs.mkdirSync(path.dirname(outputPath), {recursive: true}); fs.writeFileSync(outputPath, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({status: report.status, output: outputPath, fixed_depth_mismatches: depthMismatches,
    fixed_time_mismatches: timeMismatches, q_max_ply: report.stack_safety.q_max_ply,
    fixed_depth: report.search.fixed_depth.aggregate, fixed_time: report.search.fixed_time.aggregate}, null, 2));
  if (report.status !== 'PASS') process.exitCode = 1;
}
main().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
