#!/usr/bin/env node
/* Compare native Release with the separately-built actual Node-compatible WASM. */
const fs = require('node:fs');
const path = require('node:path');
const { spawn } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const corpusPath = path.join(root, 'tests/data/wasm-parity-100.fen');
const nativePath = process.env.HEBICHESS_NATIVE || path.join(root, 'build-release/HebiChess');
const wasmPath = process.env.HEBICHESS_WASM_NODE || path.join(root, 'build-wasm/node-test/hebichess-node.js');
const args = process.argv.slice(2);
const value = (name, fallback) => { const i = args.indexOf(name); return i < 0 ? fallback : Number(args[i + 1]); };
const depth = value('--depth', 4);
const start = value('--start', 0);
const count = value('--count', depth === 5 ? 50 : depth === 6 ? 20 : 100);
const corpus = fs.readFileSync(corpusPath, 'utf8').split(/\r?\n/).filter(x => x && !x.startsWith('#'));
if (corpus.length !== 100 || new Set(corpus).size !== 100) throw Error('corpus must contain 100 unique FENs');
if (!fs.existsSync(wasmPath)) throw Error(`missing actual WASM Node artifact: ${wasmPath}`);

const parse = (lines, depthWanted) => {
  const allowed = /^(info depth \d+ score (?:cp -?\d+|mate -?\d+)(?: .*)?|info string .+|bestmove (?:[a-h][1-8][a-h][1-8][qrbn]?|none)|id .+|option .+|uciok|readyok)$/;
  const malformed = lines.filter(line => line && !allowed.test(line));
  const infos = lines.map(line => line.match(/^info depth (\d+) score (cp|mate) (-?\d+)/)).filter(Boolean);
  const final = infos.filter(m => Number(m[1]) <= depthWanted).at(-1);
  const best = lines.find(line => line.startsWith('bestmove '));
  return { bestmove: best?.split(/\s+/)[1] || null, score: final ? (final[2] === 'cp' ? Number(final[3]) : `mate ${final[3]}`) : null, completedDepth: final ? Number(final[1]) : 0, malformed };
};

function nativeSession() {
  const child = spawn(nativePath, [], { stdio: ['pipe', 'pipe', 'inherit'] });
  const pending = [];
  let buffer = '';
  child.stdout.on('data', data => { buffer += data; while (buffer.includes('\n')) { const i = buffer.indexOf('\n'); pending.push(buffer.slice(0, i).replace(/\r$/, '')); buffer = buffer.slice(i + 1); } });
  const wait = () => new Promise((resolve, reject) => { const poll = () => pending.length ? resolve(pending.shift()) : child.exitCode !== null ? reject(Error('native engine exited')) : setImmediate(poll); poll(); });
  const run = async command => { child.stdin.write(`${command}\n`); const lines = []; if (command.startsWith('go ')) { for (;;) { const line = await wait(); lines.push(line); if (line.startsWith('bestmove ')) return parse(lines, depth); } } return lines; };
  return { run, close: () => { child.stdin.write('quit\n'); child.kill(); } };
}

async function wasmFactory() {
  const loaded = require(wasmPath);
  const factory = typeof loaded === 'function' ? loaded : loaded.default || loaded.createHebiChessNodeTestModule;
  if (typeof factory !== 'function') throw Error('Node WASM artifact did not export a module factory');
  const module = await factory({ locateFile: file => path.join(path.dirname(wasmPath), file) });
  if (!module.ccall) throw Error('Node WASM module has no ccall runtime API');
  module.ccall('hebichess_initialize', null, [], []);
  return module;
}
function wasmCommand(module, command) { module.ccall('hebichess_send_command', null, ['string'], [command]); return module.ccall('hebichess_take_output', 'string', [], []).split(/\r?\n/).filter(Boolean); }
async function wasmRun(module, fen) { wasmCommand(module, 'ucinewgame'); wasmCommand(module, `position fen ${fen}`); return parse(wasmCommand(module, `go depth ${depth}`), depth); }

async function main() {
  const end = Math.min(start + count, corpus.length), native = nativeSession(), expected = [];
  try { for (let i = start; i < end; i++) { await native.run('ucinewgame'); await native.run(`position fen ${corpus[i]}`); expected.push({ index: i, fen: corpus[i], ...(await native.run(`go depth ${depth}`)) }); } } finally { native.close(); }
  const persistentModule = await wasmFactory(), actual = [];
  try { for (const item of expected) actual.push({ index: item.index, fen: item.fen, ...(await wasmRun(persistentModule, item.fen)) }); } catch (error) { actual.push({ status: 'crash', error: String(error) }); }
  const mismatches = expected.map((want, i) => ({ want, got: actual[i] })).filter(({ want, got }) => !got || got.status === 'crash' || want.bestmove !== got.bestmove || String(want.score) !== String(got.score) || want.malformed.length || got.malformed.length);

  const stateFens = corpus.slice(0, 25), fresh = [];
  for (const fen of stateFens) { const module = await wasmFactory(); try { fresh.push(await wasmRun(module, fen)); } finally { /* module lifetime ends with the session */ } }
  const statePersistent = [], stateModule = await wasmFactory();
  for (const fen of stateFens) statePersistent.push(await wasmRun(stateModule, fen));
  const persistentFresh = statePersistent.every((item, i) => item.bestmove === fresh[i].bestmove && String(item.score) === String(fresh[i].score));
  const summary = { at: new Date().toISOString(), actualWasm: true, wasmArtifact: wasmPath, corpus: corpusPath, depth, start, count: expected.length, expected, actual, mismatches, persistentFresh, persistentCount: stateFens.length };
  const parityDir = path.join(root, 'build-wasm/parity'); fs.mkdirSync(parityDir, { recursive: true });
  const finish = end ? end - 1 : start; const file = path.join(parityDir, `depth${depth}-${String(start).padStart(3, '0')}-${String(finish).padStart(3, '0')}.json`);
  fs.writeFileSync(file, JSON.stringify(summary, null, 2) + '\n');
  console.log(JSON.stringify({ ...summary, file }, null, 2));
  if (mismatches.length || !persistentFresh) process.exitCode = 1;
}
main().catch(error => { console.error(error.stack || error); process.exitCode = 1; });
