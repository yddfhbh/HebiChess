const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

const knownFixtures = [
  ['endgame', '8/2b2p2/2p3p1/p1k4p/K7/1B4P1/7P/8 b - - 0 1', 10],
  ['startpos', 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1', 5],
  ['quiet', 'rn1qk3/2p1p1b1/pp3npr/3p1p1p/P2P2P1/RPN4b/1BP1PP1P/1Q2KBNR w Kq - 1 12', 5],
  ['tactical', 'rn1q1k2/4p2r/ppp2npb/5p2/P2Pp3/BPN5/R1P1KPbP/1Q3BNR w - - 2 18', 5],
  ['king_attack', '2r1n1k1/3np2r/1ppq4/p1BNb1pQ/P1BPp3/1P6/2P2PbP/1R2K1NR w - - 14 30', 5],
];

function loadFixtures(corpusPath) {
  const corpus = fs.readFileSync(corpusPath, 'utf8').split(/\r?\n/)
    .filter(line => line && !line.startsWith('#'));
  const sampled = corpus.filter((_, index) => index % 6 === 0).slice(0, 15)
    .map((line, index) => [`corpus-${index}`, line.split('\t').at(-1), 4]);
  if (sampled.length < 15) throw new Error('deterministic corpus has fewer than 15 sampled positions');
  return [...knownFixtures, ...sampled];
}

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

function runOne(engine, fixture) {
  const { mod } = engine;
  command(mod, 'ucinewgame');
  command(mod, `position fen ${fixture[1]}`);
  const start = performance.now();
  const out = command(mod, `go depth ${fixture[2]}`);
  const elapsed = performance.now() - start;
  const stats = out.match(/info string nodes (\d+) main_nodes (\d+) qnodes (\d+).*?aspiration_retries (\d+)/s);
  const best = out.match(/bestmove (\S+)/);
  const pv = out.match(/info string reuse pv(.*?) expected_reply/s);
  const depths = [...out.matchAll(/info depth (\d+) score (?:cp|mate) (-?\d+) nodes (\d+) qnodes (\d+)/g)];
  const cache = out.match(/info string evalcache_exact ([^\r\n]+)/);
  if (!stats || !best || !pv) throw new Error(`${fixture[0]}: could not parse UCI output: ${out}`);
  const last = depths.at(-1);
  return {
    elapsed,
    result: {
      best: best[1], depth: Number(last?.[1] ?? 0), score: Number(last?.[2] ?? 0),
      nodes: Number(stats[1]), qnodes: Number(stats[3]), retries: Number(stats[4]), pv: pv[1].trim(),
    },
    cache: cache ? keyValues(cache[1]) : null,
  };
}

function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.floor(sorted.length / 2)];
}

async function main() {
  const [offPath, onPath, networkPath, corpusPath, roundsArg, onlyName, depthArg] = process.argv.slice(2);
  const rounds = roundsArg ? Number(roundsArg) : 4;
  if (!offPath || !onPath || !networkPath || !corpusPath)
    throw new Error('usage: eval_cache_wasm_benchmark.cjs OFF_MODULE ON_MODULE NETWORK CORPUS [ROUNDS]');
  let fixtures = loadFixtures(corpusPath);
  if (onlyName) fixtures = fixtures.filter(fixture => fixture[0] === onlyName);
  if (depthArg) fixtures = fixtures.map(fixture => [fixture[0], fixture[1], Number(depthArg)]);
  if (fixtures.length === 0) throw new Error(`no fixture named ${onlyName}`);
  const [off, on] = await Promise.all([loadEngine(offPath, networkPath), loadEngine(onPath, networkPath)]);
  const speeds = [];
  let parityFailures = 0;
  for (const fixture of fixtures) {
    const times = { off: [], on: [] };
    let ref = null;
    let cacheStats = null;
    runOne(off, fixture);
    runOne(on, fixture);
    for (let round = 0; round < rounds; round++) {
      const order = round % 2 === 0 ? [['off', off], ['on', on]] : [['on', on], ['off', off]];
      for (const [label, engine] of order) {
        const sample = runOne(engine, fixture);
        times[label].push(sample.elapsed);
        if (label === 'off') ref = sample.result;
        else {
          if (JSON.stringify(sample.result) !== JSON.stringify(ref)) parityFailures++;
          cacheStats = sample.cache;
        }
      }
    }
    const offMs = median(times.off), onMs = median(times.on);
    const speedPct = 100 * (offMs / onMs - 1);
    speeds.push(speedPct);
    console.log(JSON.stringify({
      name: fixture[0], depth: fixture[2], off_median_ms: Number(offMs.toFixed(3)),
      on_median_ms: Number(onMs.toFixed(3)), speedup_pct: Number(speedPct.toFixed(2)),
      ...ref, parity: parityFailures === 0, cache: cacheStats,
      off_linear_memory_bytes: off.memory, on_linear_memory_bytes: on.memory,
      off_rss_init_bytes: off.rss, on_rss_init_bytes: on.rss,
    }));
  }
  const gm = Math.exp(speeds.reduce((sum, value) => sum + Math.log(1 + value / 100), 0) / speeds.length);
  const sorted = [...speeds].sort((a, b) => a - b);
  console.log(JSON.stringify({
    summary: true, positions: fixtures.length, rounds, parity_failures: parityFailures,
    geomean_speedup_pct: Number((100 * (gm - 1)).toFixed(2)),
    median_speedup_pct: Number(sorted[Math.floor(sorted.length / 2)].toFixed(2)),
    faster_gt_1pct: speeds.filter(value => value > 1).length,
    neutral_within_1pct: speeds.filter(value => Math.abs(value) <= 1).length,
    slower_lt_minus_1pct: speeds.filter(value => value < -1).length,
    worst_speedup_pct: Number(Math.min(...speeds).toFixed(2)),
    best_speedup_pct: Number(Math.max(...speeds).toFixed(2)),
  }));
}

main().catch(error => { console.error(error); process.exitCode = 1; });
