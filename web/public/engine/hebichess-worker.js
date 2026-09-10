/* global importScripts */
// Experimental single-threaded engine: search never runs on the UI thread.
let modulePromise, module, liveContext = {};
function diagnostic(step, extra = {}) { self.postMessage({type:'diagnostic', step, at:performance.now(), ...extra}); }
function parseInfo(line) {
  const depth=line.match(/\bdepth\s+(\d+)/), score=line.match(/\bscore\s+(cp|mate)\s+(-?\d+)/), nodes=line.match(/\bnodes\s+(\d+)/);
  return {scoreType:score?.[1]||null, cp:score?.[1]==='cp'?Number(score[2]):null, mate:score?.[1]==='mate'?Number(score[2]):null, depth:depth?Number(depth[1]):null, nodes:nodes?Number(nodes[1]):null};
}

self.hebichessOutputLine = line => {
  const context = liveContext;
  if (line.startsWith('info ')) {
    diagnostic('info received', {line, parsed:parseInfo(line), ...context});
    self.postMessage({type:'info', line, live:true, ...context});
  } else if (line.startsWith('bestmove ')) {
    diagnostic('bestmove received by worker', {line, ...context});
    self.postMessage({type:'bestmove', move:line.split(/\s+/)[1], live:true, ...context});
  } else self.postMessage({type:'output', line, live:true, ...context});
};

function emitOutput(context = {}) {
  const text = module.ccall('hebichess_take_output', 'string', [], []);
  for (const line of text.split(/\r?\n/)) {
    if (!line) continue;
    if (line === 'uciok') diagnostic('uciok received', {line, ...context});
    if (line === 'readyok') diagnostic('readyok received', {line, ...context});
    // Browser builds emit info/bestmove through hebichessOutputLine while the
    // synchronous ccall is still running. Do not send those lines again when
    // draining the compatibility output buffer after the call returns.
    if (!line.startsWith('bestmove ') && !line.startsWith('info ')) self.postMessage({type:'output', line, ...context});
  }
}

async function initialize() {
  diagnostic('worker initialize');
  if (!modulePromise) {
    const engineScriptUrl = new URL('hebichess.js', self.location.href).href;
    const engineWasmUrl = new URL('hebichess.wasm', engineScriptUrl).href;
    diagnostic('engine artifacts resolved', {engineScriptUrl, engineWasmUrl});
    importScripts(engineScriptUrl);
    modulePromise = self.createHebiChessModule({locateFile:file => new URL(file, engineScriptUrl).href});
  }
  module = await modulePromise;
  module.ccall('hebichess_initialize', null, [], []);
  diagnostic('worker engine initialized');
  emitOutput();
}

function command(text, context) {
  if (text === 'uci') diagnostic('uci sent', context);
  if (text === 'isready') diagnostic('isready sent', context);
  if (text.startsWith('go ')) diagnostic(`${text} sent`, context);
  diagnostic('bridge command sent', {command:text, ...context});
  liveContext = context;
  module.ccall('hebichess_send_command', null, ['string'], [text]);
  liveContext = {};
  diagnostic('bridge command returned', {command:text, ...context});
  emitOutput(context);
  diagnostic('output callback drained', {command:text, ...context});
}

self.onmessage = async ({data:message = {}}) => {
  try {
    if (message.type === 'init') { await initialize(); diagnostic('worker ready'); self.postMessage({type:'ready'}); return; }
    if (!module) throw Error('engine is not initialized');
    if (message.type === 'position') {
      const base = message.fen ? `position fen ${message.fen}` : 'position startpos';
      command(message.moves?.length ? `${base} moves ${message.moves.join(' ')}` : base, {gameId:message.gameId});
    } else if (message.type === 'go') {
      const limits = Number.isFinite(Number(message.depth)) ? `depth ${Math.max(1, Number(message.depth))}` : `movetime ${Math.max(1, Number(message.movetime) || 1)}`;
      command(`go ${limits}`, {gameId:message.gameId, searchId:message.searchId});
    } else if (message.type === 'command') command(String(message.command || ''), {gameId:message.gameId, searchId:message.searchId});
    else throw Error(`unsupported worker message: ${message.type}`);
  } catch (error) { self.postMessage({type:'error', message:String(error.message || error), gameId:message.gameId, searchId:message.searchId}); }
};
