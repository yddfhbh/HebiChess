const fs = require('node:fs');
const { spawnSync } = require('node:child_process');
const path = require('node:path');

const [engine, stockfishNet, stockfish14Net, hebiNet] = process.argv.slice(2);
if (!engine || !stockfishNet || !stockfish14Net) {
  throw new Error('usage: node stockfish_nnue_native_benchmark.cjs <engine.exe> <sf19.nnue> <sf14.nnue> [hebinnue]');
}

const fen = 'r1bqkb1r/1p3ppp/p2ppn2/2n5/2BNP2P/2N1BQ2/PPP2PP1/R3K2R b KQkq - 2 9';
const limits = ['depth 5', 'depth 6'];
const commands = [
  'uci',
  `setoption name StockfishEvalFile value ${path.resolve(stockfishNet)}`,
  `setoption name Stockfish14EvalFile value ${path.resolve(stockfish14Net)}`,
  'isready',
];
if (hebiNet && hebiNet !== '-') commands.splice(3, 0, `setoption name EvalFile value ${path.resolve(hebiNet)}`);
const modes = hebiNet && hebiNet !== '-'
  ? ['HCE', 'NNUE', 'StockfishNNUE', 'Stockfish14NNUE']
  : ['HCE', 'StockfishNNUE', 'Stockfish14NNUE'];
for (const mode of modes) {
  commands.push(`setoption name EvalMode value ${mode}`);
  for (const limit of limits) {
    commands.push('ucinewgame', `position fen ${fen}`, `go ${limit}`);
  }
}
commands.push('quit');

const run = spawnSync(path.resolve(engine), [], {
  input: `${commands.join('\n')}\n`,
  encoding: 'utf8',
  maxBuffer: 16 * 1024 * 1024,
});
if (run.error || run.status !== 0) throw run.error || new Error(run.stderr || `engine exit ${run.status}`);
const lines = run.stdout.split(/\r?\n/).filter(Boolean);
const chunks = [];
let pending = [];
for (const line of lines) {
  pending.push(line);
  if (line.startsWith('bestmove ')) {
    chunks.push(pending);
    pending = [];
  }
}
if (chunks.length !== modes.length * limits.length) throw new Error(`expected ${modes.length * limits.length} completed searches, found ${chunks.length}\n${run.stdout}`);

function field(lines, sourcePrefix, name) {
  const line = lines.find(value => value.startsWith(sourcePrefix)) || '';
  const match = line.match(new RegExp(`\\b${name} (\\d+)\\b`));
  return match ? Number(match[1]) : null;
}

const rows = chunks.map((block, index) => {
  const finalInfo = block.filter(line => /^info depth \d+ score /.test(line)).at(-1) || '';
  const depth = finalInfo.match(/^info depth (\d+)/);
  const score = finalInfo.match(/\bscore (cp|mate) (-?\d+)/);
  const bestmove = block.find(line => line.startsWith('bestmove ')) || '';
  const mode = block.find(line => line.startsWith('info string eval_profile mode '))?.match(/mode (\w+)/)?.[1] || '';
  const modeRun = [...chunks.slice(0, index + 1)].filter(chunk =>
    chunk.some(line => line.startsWith(`info string eval_profile mode ${mode} `))).length - 1;
  const label = limits[modeRun];
  const time = block.find(line => line.startsWith('info string tm ')) || '';
  return {
    mode,
    limit: label,
    bestmove: bestmove.split(/\s+/)[1] || null,
    score: score ? `${score[1]} ${score[2]}` : null,
    reached_depth: depth ? Number(depth[1]) : 0,
    nodes: field(block, 'info string nodes ', 'nodes') ?? (finalInfo.match(/\bnodes (\d+)/)?.[1] ?? null),
    qnodes: field(block, 'info string nodes ', 'qnodes') ?? (finalInfo.match(/\bqnodes (\d+)/)?.[1] ?? null),
    qnode_percent: (() => {
      const nodes = field(block, 'info string nodes ', 'nodes') ?? Number(finalInfo.match(/\bnodes (\d+)/)?.[1] ?? 0);
      const qnodes = field(block, 'info string nodes ', 'qnodes') ?? Number(finalInfo.match(/\bqnodes (\d+)/)?.[1] ?? 0);
      return nodes ? Number((100 * qnodes / nodes).toFixed(2)) : null;
    })(),
    nps: field(block, 'info string tm ', 'nps'),
    elapsed_ms: field(block, 'info string tm ', 'elapsed'),
    evaluator_calls: field(block, 'info string eval_profile ', 'evaluator_calls'),
    accumulator_updates: field(block, 'info string eval_profile ', 'accumulator_updates'),
    sf_accumulator_transform_requests: field(block, 'info string eval_profile ', 'sf_accumulator_transform_requests'),
    nnue_evals: field(block, 'info string qprofile ', 'nnue_evals'),
    nnue_updates: field(block, 'info string qprofile ', 'nnue_updates'),
    protocol_ok: Boolean(bestmove && finalInfo && !block.some(line => line.startsWith('info string error '))),
    _tm_line: time,
  };
});
for (const row of rows) delete row._tm_line;
const result = { engine: path.resolve(engine), rows };
const report = `${path.resolve(engine)}.benchmark.json`;
fs.writeFileSync(report, `${JSON.stringify(result, null, 2)}\n`);
console.log(JSON.stringify({ ...result, report }, null, 2));
