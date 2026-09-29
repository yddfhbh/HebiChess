const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

const [artifact, stockfishNet, stockfish14Net, hebiNet] = process.argv.slice(2);
if (!artifact || !stockfishNet || !stockfish14Net) {
  throw new Error('usage: node stockfish_nnue_wasm_benchmark.cjs <wasm-js> <sf19.nnue> <sf14.nnue> [hebinnue]');
}

const fen = 'r1bqkb1r/1p3ppp/p2ppn2/2n5/2BNP2P/2N1BQ2/PPP2PP1/R3K2R b KQkq - 2 9';
const modes = [
  ['HCE', null],
  ...(hebiNet && hebiNet !== '-' ? [['NNUE', 'hebichess_nnue_load_bytes']] : []),
  ['StockfishNNUE', 'hebichess_stockfish_nnue_load_bytes'],
  ['Stockfish14NNUE', 'hebichess_stockfish14_nnue_load_bytes'],
];
const limits = ['depth 5', 'depth 6', 'movetime 1000', 'movetime 3000', 'movetime 5000'];

const factoryExport = require(path.resolve(artifact));
const factory = typeof factoryExport === 'function' ? factoryExport : factoryExport.default;
const bytesForStockfish = fs.readFileSync(stockfishNet);
const bytesForStockfish14 = fs.readFileSync(stockfish14Net);
const bytesForHebi = hebiNet && hebiNet !== '-' ? fs.readFileSync(hebiNet) : null;

function take(module) {
  return module.ccall('hebichess_take_output', 'string', [], []) || '';
}

function command(module, text) {
  module.ccall('hebichess_send_command', null, ['string'], [text]);
  return take(module).split(/\r?\n/).filter(Boolean);
}

function loadBytes(module, name, bytes) {
  const pointer = module._malloc(bytes.length);
  try {
    module.HEAPU8.set(bytes, pointer);
    const started = performance.now();
    const loaded = module.ccall(name, 'number', ['number', 'number'], [pointer, bytes.length]);
    return { loaded: loaded === 1, elapsed_ms: performance.now() - started };
  } finally {
    module._free(pointer);
  }
}

function parse(lines, wallMs) {
  const finalInfo = lines.filter(line => /^info depth \d+ score /.test(line)).at(-1) || '';
  const read = (line, field) => {
    const match = line.match(new RegExp(`\\b${field} (\\d+)\\b`));
    return match ? Number(match[1]) : null;
  };
  const depth = finalInfo.match(/^info depth (\d+)/);
  const score = finalInfo.match(/\bscore (cp|mate) (-?\d+)/);
  const best = lines.find(line => line.startsWith('bestmove '));
  const timeLine = lines.find(line => line.startsWith('info string tm ')) || '';
  const stats = lines.find(line => line.startsWith('info string nodes ')) || '';
  const profile = lines.find(line => line.startsWith('info string eval_profile ')) || '';
  const qprofile = lines.find(line => line.startsWith('info string qprofile ')) || '';
  return {
    bestmove: best ? best.split(/\s+/)[1] : null,
    score: score ? `${score[1]} ${score[2]}` : null,
    reached_depth: depth ? Number(depth[1]) : 0,
    nodes: read(stats, 'nodes') ?? read(finalInfo, 'nodes'),
    qnodes: read(stats, 'qnodes') ?? read(finalInfo, 'qnodes'),
    qnode_percent: (() => {
      const nodes = read(stats, 'nodes') ?? Number(finalInfo.match(/\bnodes (\d+)/)?.[1] ?? 0);
      const qnodes = read(stats, 'qnodes') ?? Number(finalInfo.match(/\bqnodes (\d+)/)?.[1] ?? 0);
      return nodes ? Number((100 * qnodes / nodes).toFixed(2)) : null;
    })(),
    nps: read(timeLine, 'nps'),
    elapsed_ms: read(timeLine, 'elapsed') ?? wallMs,
    evaluator_calls: read(profile, 'evaluator_calls'),
    accumulator_updates: read(profile, 'accumulator_updates'),
    sf_accumulator_transform_requests: read(profile, 'sf_accumulator_transform_requests'),
    qnodes_profile: read(qprofile, 'qnodes'),
    protocol_ok: Boolean(best && finalInfo && lines.every(line => !line.startsWith('info string error '))),
    wall_elapsed_ms: wallMs,
  };
}

(async () => {
  const factoryStarted = performance.now();
  const module = await factory({ locateFile: file => path.join(path.dirname(path.resolve(artifact)), file) });
  const moduleInitMs = performance.now() - factoryStarted;
  module.ccall('hebichess_initialize', null, [], []);
  const memoryAfterInit = module.HEAPU8.buffer.byteLength;

  const failedLoad = loadBytes(module, 'hebichess_stockfish_nnue_load_bytes', bytesForStockfish.subarray(0, 6));
  if (failedLoad.loaded) throw new Error('truncated Stockfish network unexpectedly loaded');
  const stockfishLoad = loadBytes(module, 'hebichess_stockfish_nnue_load_bytes', bytesForStockfish);
  if (!stockfishLoad.loaded) throw new Error(`Stockfish NNUE network load failed: ${take(module)}`);
  const stockfishEval = module.ccall('hebichess_stockfish_nnue_evaluate_fen', 'number', ['string'], [fen]);
  const stockfishEvalAgain = module.ccall('hebichess_stockfish_nnue_evaluate_fen', 'number', ['string'], [fen]);
  if (stockfishEval !== stockfishEvalAgain) throw new Error('WASM Stockfish repeated eval mismatch');
  if (stockfishEval !== -15) throw new Error(`native/WASM scalar reference eval mismatch: ${stockfishEval}`);

  const failedStockfish14Load = loadBytes(module, 'hebichess_stockfish14_nnue_load_bytes', bytesForStockfish14.subarray(0, 6));
  if (failedStockfish14Load.loaded) throw new Error('truncated Stockfish 14 network unexpectedly loaded');
  const stockfish14Load = loadBytes(module, 'hebichess_stockfish14_nnue_load_bytes', bytesForStockfish14);
  if (!stockfish14Load.loaded) throw new Error(`Stockfish 14 NNUE load failed: ${take(module)}`);
  const stockfish14Eval = module.ccall('hebichess_stockfish14_nnue_evaluate_fen', 'number', ['string'], [fen]);
  const stockfish14EvalAgain = module.ccall('hebichess_stockfish14_nnue_evaluate_fen', 'number', ['string'], [fen]);
  if (stockfish14Eval !== stockfish14EvalAgain) throw new Error('WASM Stockfish 14 repeated eval mismatch');
  if (stockfish14Eval !== -12) throw new Error(`native/WASM Stockfish 14 eval mismatch: ${stockfish14Eval}`);

  const customLoad = bytesForHebi
    ? loadBytes(module, 'hebichess_nnue_load_bytes', bytesForHebi)
    : null;
  if (customLoad && !customLoad.loaded) throw new Error(`HebiChess NNUE network load failed: ${take(module)}`);
  const memoryAfterNetworks = module.HEAPU8.buffer.byteLength;
  const rows = [];
  for (const [mode, loader] of modes) {
    if (loader && mode === 'NNUE') {
      const loaded = loadBytes(module, loader, bytesForHebi);
      if (!loaded.loaded) throw new Error(`HebiChess NNUE reload failed: ${take(module)}`);
    } else if (loader) {
      const useSf14 = mode === 'Stockfish14NNUE';
      const model = useSf14 ? bytesForStockfish14 : bytesForStockfish;
      const loaded = loadBytes(module, loader, model);
      if (!loaded.loaded) throw new Error(`${mode} reload failed: ${take(module)}`);
    }
    if (mode === 'Stockfish14NNUE')
      module.ccall('hebichess_stockfish14_nnue_reset_stats', null, [], []);
    const setMode = command(module, `setoption name EvalMode value ${mode}`);
    if (setMode.some(line => line.startsWith('info string error '))) throw new Error(setMode.join('\n'));
    for (const limit of limits) {
      command(module, 'ucinewgame');
      command(module, `position fen ${fen}`);
      const started = performance.now();
      const lines = command(module, `go ${limit}`);
      const result = parse(lines, performance.now() - started);
      if (!result.protocol_ok) throw new Error(`bad ${mode} ${limit} result:\n${lines.join('\n')}`);
      const sf14Stats = mode === 'Stockfish14NNUE'
        ? JSON.parse(module.ccall('hebichess_stockfish14_nnue_stats_json', 'string', [], []))
        : null;
      rows.push({ mode, limit, ...result,
        sf14_evaluator_calls: sf14Stats?.eval_calls ?? null,
        sf14_accumulator_updates: sf14Stats?.accumulator_updates ?? null,
        sf14_failures: sf14Stats?.failures ?? null });
    }
  }
  const output = {
    artifact: path.resolve(artifact),
    stockfish_network_bytes: bytesForStockfish.length,
    stockfish14_network_bytes: bytesForStockfish14.length,
    custom_network_bytes: bytesForHebi?.length ?? null,
    module_init_ms: moduleInitMs,
    wasm_linear_memory_after_init_bytes: memoryAfterInit,
    wasm_linear_memory_after_networks_bytes: memoryAfterNetworks,
    process_rss_bytes: process.memoryUsage().rss,
    stockfish_load_ms: stockfishLoad.elapsed_ms,
    stockfish14_load_ms: stockfish14Load.elapsed_ms,
    custom_load_ms: customLoad?.elapsed_ms ?? null,
    stockfish_regression_eval_cp: stockfishEval,
    stockfish14_regression_eval_cp: stockfish14Eval,
    stockfish_repeat_eval_match: stockfishEval === stockfishEvalAgain,
    invalid_stockfish_load_rejected: !failedLoad.loaded,
    invalid_stockfish14_load_rejected: !failedStockfish14Load.loaded,
    process_rss_after_search_bytes: process.memoryUsage().rss,
    wasm_linear_memory_after_search_bytes: module.HEAPU8.buffer.byteLength,
    rows,
  };
  const report = `${path.resolve(artifact)}.benchmark.json`;
  fs.writeFileSync(report, `${JSON.stringify(output, null, 2)}\n`);
  console.log(JSON.stringify({ ...output, report }, null, 2));
})().catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
