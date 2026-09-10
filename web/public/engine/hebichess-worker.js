/* global importScripts */
// Experimental single-threaded engine: search never runs on the UI thread.
let modulePromise, module;
function diagnostic(step, extra = {}) { self.postMessage({type:'diagnostic', step, at:performance.now(), ...extra}); }

function emitOutput(context = {}) {
  const text = module.ccall('hebichess_take_output', 'string', [], []);
  for (const line of text.split(/\r?\n/)) {
    if (!line) continue;
    if (line === 'uciok') diagnostic('uciok received', {line, ...context});
    if (line === 'readyok') diagnostic('readyok received', {line, ...context});
    if (line.startsWith('info ')) diagnostic('info received', {line, ...context});
    if (line.startsWith('bestmove ')) { diagnostic('bestmove received by worker', {line, ...context}); self.postMessage({type:'bestmove', move:line.split(/\s+/)[1], ...context}); }
    else if (line.startsWith('info ')) self.postMessage({type:'info', line, ...context});
    else self.postMessage({type:'output', line, ...context});
  }
}

async function initialize() {
  diagnostic('worker initialize');
  if (!modulePromise) {
    importScripts('/public/engine/hebichess.js');
    modulePromise = self.createHebiChessModule({locateFile:file => `/public/engine/${file}`});
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
  module.ccall('hebichess_send_command', null, ['string'], [text]);
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
