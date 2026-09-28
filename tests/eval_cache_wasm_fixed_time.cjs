const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

const known = [
  ['endgame', '8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1'],
  ['startpos', 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'],
  ['quiet', 'rn1qk3/2p1p1b1/pp3npr/3p1p1p/P2P2P1/RPN4b/1BP1PP1P/1Q2KBNR w Kq - 1 12'],
  ['tactical', 'rn1q1k2/4p2r/ppp2npb/5p2/P2Pp3/BPN5/R1P1KPbP/1Q3BNR w - - 2 18'],
  ['king_attack', '2r1n1k1/3np2r/1ppq4/p1BNb1pQ/P1BPp3/1P6/2P2PbP/1R2K1NR w - - 14 30'],
];

function command(mod, value) {
  mod.ccall('hebichess_send_command', null, ['string'], [value]);
  return mod.ccall('hebichess_take_output', 'string', [], []);
}

function keyValues(line) {
  return Object.fromEntries([...line.matchAll(/([a-z_]+) (\S+)/g)]
    .map(([, key, value]) => [key, /^-?\d+$/.test(value) ? Number(value) : value]));
}

async function loadEngine(jsPath, networkPath) {
  const mod = await require(path.resolve(jsPath))();
  const network = fs.readFileSync(networkPath);
  const ptr = mod._malloc(network.length);
  mod.HEAPU8.set(network, ptr);
  if (!mod.ccall('hebichess_nnue_load_bytes', 'number', ['number', 'number'], [ptr, network.length]))
    throw new Error(`failed to load NNUE into ${jsPath}`);
  mod._free(ptr);
  mod.ccall('hebichess_initialize');
  command(mod, 'setoption name EvalMode value NNUE');
  return { mod, memory: mod.HEAPU8.buffer.byteLength, rss: process.memoryUsage().rss };
}

function runOne(engine, name, fen, ms) {
  const { mod } = engine;
  command(mod, 'ucinewgame');
  command(mod, `position fen ${fen}`);
  const started = performance.now();
  const out = command(mod, `go movetime ${ms}`);
  const wallMs = performance.now() - started;
  const tm = out.match(/info string tm ([^\r\n]+)/);
  const cache = out.match(/info string evalcache_exact ([^\r\n]+)/);
  const stats = out.match(/info string nodes (\d+) main_nodes (\d+) qnodes (\d+)/);
  const depths = [...out.matchAll(/info depth (\d+) score (?:cp|mate) (-?\d+) nodes (\d+) qnodes (\d+)/g)];
  const best = out.match(/bestmove (\S+)/);
  if (!tm || !stats || !best || !depths.length) throw new Error(`${name}: cannot parse movetime ${ms} output: ${out}`);
  return {
    name, requested_ms: ms, wall_ms: Number(wallMs.toFixed(1)), timing: keyValues(tm[1]),
    depth: Number(depths.at(-1)[1]), attempted_depth: keyValues(tm[1]).attempted_depth,
    score: Number(depths.at(-1)[2]), nodes: Number(stats[1]), main_nodes: Number(stats[2]),
    qnodes: Number(stats[3]), bestmove: best[1], cache: cache ? keyValues(cache[1]) : null,
    linear_memory_bytes: mod.HEAPU8.buffer.byteLength,
    rss_bytes: process.memoryUsage().rss,
  };
}

async function main() {
  const [offPath, onPath, networkPath, corpusPath] = process.argv.slice(2);
  if (!offPath || !onPath || !networkPath || !corpusPath)
    throw new Error('usage: eval_cache_wasm_fixed_time.cjs OFF_MODULE ON_MODULE NETWORK CORPUS');
  const source = fs.readFileSync(corpusPath, 'utf8').split(/\r?\n/).filter(line => line && !line.startsWith('#'));
  const sampled = source.filter((_, index) => index % 18 === 0).slice(0, 5)
    .map((line, index) => [`corpus-${index}`, line.split('\t').at(-1)]);
  if (sampled.length < 5) throw new Error('deterministic corpus has fewer than five sampled positions');
  const positions = [...known, ...sampled];
  const [off, on] = await Promise.all([loadEngine(offPath, networkPath), loadEngine(onPath, networkPath)]);
  const nodeRatios = new Map([[1000, []], [3000, []], [5000, []]]);
  let runIndex = 0;
  for (const [name, fen] of positions) {
    const budgets = name.startsWith('corpus-') ? [1000, 3000] : [1000, 3000, 5000];
    for (const ms of budgets) {
      const ordered = runIndex++ % 2 === 0 ? [['off', off], ['on', on]] : [['on', on], ['off', off]];
      const results = {};
      for (const [label, engine] of ordered) results[label] = runOne(engine, name, fen, ms);
      const ratio = results.on.nodes / Math.max(1, results.off.nodes);
      nodeRatios.get(ms).push(ratio);
      console.log(JSON.stringify({
        position: name, requested_ms: ms, off: results.off, on: results.on,
        node_increase_pct: Number((100 * (ratio - 1)).toFixed(2)),
      }));
    }
  }
  for (const [ms, ratios] of nodeRatios) {
    if (!ratios.length) continue;
    const sorted = [...ratios].sort((a, b) => a - b);
    const gm = Math.exp(ratios.reduce((sum, value) => sum + Math.log(value), 0) / ratios.length);
    console.log(JSON.stringify({
      summary: true, requested_ms: ms, positions: ratios.length,
      geomean_node_increase_pct: Number((100 * (gm - 1)).toFixed(2)),
      median_node_increase_pct: Number((100 * (sorted[Math.floor(sorted.length / 2)] - 1)).toFixed(2)),
      more_nodes: ratios.filter(value => value > 1).length,
      equal_nodes: ratios.filter(value => value === 1).length,
      fewer_nodes: ratios.filter(value => value < 1).length,
    }));
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
