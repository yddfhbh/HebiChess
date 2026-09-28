const fs = require('node:fs');
const path = require('node:path');
const { performance } = require('node:perf_hooks');

function command(mod, value) {
  mod.ccall('hebichess_send_command', null, ['string'], [value]);
  return mod.ccall('hebichess_take_output', 'string', [], []);
}

async function main() {
  const [modulePath, networkPath, fen] = process.argv.slice(2);
  if (!modulePath || !networkPath || !fen)
    throw new Error('usage: eval_cache_wasm_memory.cjs MODULE NETWORK FEN');
  const measurements = { rss_before_module_bytes: process.memoryUsage().rss };
  const moduleStart = performance.now();
  const mod = await require(path.resolve(modulePath))();
  measurements.module_factory_ms = Number((performance.now() - moduleStart).toFixed(2));
  measurements.linear_memory_after_factory_bytes = mod.HEAPU8.buffer.byteLength;
  measurements.rss_after_factory_bytes = process.memoryUsage().rss;
  const network = fs.readFileSync(networkPath);
  const ptr = mod._malloc(network.length);
  mod.HEAPU8.set(network, ptr);
  if (!mod.ccall('hebichess_nnue_load_bytes', 'number', ['number', 'number'], [ptr, network.length]))
    throw new Error('failed to load NNUE');
  mod._free(ptr);
  const initStart = performance.now();
  mod.ccall('hebichess_initialize');
  measurements.initialize_ms = Number((performance.now() - initStart).toFixed(2));
  measurements.linear_memory_after_initialize_bytes = mod.HEAPU8.buffer.byteLength;
  measurements.rss_after_initialize_bytes = process.memoryUsage().rss;
  command(mod, 'setoption name EvalMode value NNUE');
  command(mod, 'ucinewgame');
  command(mod, `position fen ${fen}`);
  const searchStart = performance.now();
  const output = command(mod, 'go depth 5');
  measurements.first_search_ms = Number((performance.now() - searchStart).toFixed(2));
  measurements.linear_memory_after_first_search_bytes = mod.HEAPU8.buffer.byteLength;
  measurements.rss_after_first_search_bytes = process.memoryUsage().rss;
  const stats = output.match(/info string nodes (\d+) main_nodes (\d+) qnodes (\d+)/);
  const cache = output.match(/info string evalcache_exact ([^\r\n]+)/);
  measurements.nodes = stats ? Number(stats[1]) : null;
  measurements.qnodes = stats ? Number(stats[3]) : null;
  measurements.cache = cache ? Object.fromEntries([...cache[1].matchAll(/([a-z_]+) (\S+)/g)]
    .map(([, key, value]) => [key, /^-?\d+$/.test(value) ? Number(value) : value])) : null;
  console.log(JSON.stringify(measurements));
}

main().catch(error => { console.error(error); process.exitCode = 1; });
