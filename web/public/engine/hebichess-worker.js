/* global importScripts */
// The Worker owns all engine state.  NNUE bytes never enter the UI thread.
let modulePromise, module, liveContext = {};
let evalMode = 'HCE';
let nnueState = 'hce-ready';
let bookState = 'book-unavailable';
const frozenModel = {
  file: 'models/hebinnue-v3-4c815d54bc6c9fbf.hebinnue',
  sha256: '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826'
};
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
  return text;
}

function publishNnue(extra = {}) { self.postMessage({type:'nnue-state', state:nnueState, evalMode, ...extra}); }
function publishBook(extra = {}) { self.postMessage({type:'book-state', state:bookState, ...extra}); }
function hex(bytes) { return Array.from(new Uint8Array(bytes), byte => byte.toString(16).padStart(2, '0')).join(''); }
async function sha256(bytes) {
  if (!self.crypto?.subtle) throw Error('SHA-256 validation requires Web Crypto (secure context or localhost)');
  return hex(await self.crypto.subtle.digest('SHA-256', bytes));
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
  publishNnue();
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
  const output = emitOutput(context);
  diagnostic('output callback drained', {command:text, ...context});
  return output;
}

async function loadNnue({url, sha256: expectedSha256} = {}) {
  if (nnueState === 'nnue-ready') { publishNnue(); return; }
  nnueState = 'nnue-loading'; publishNnue();
  let bytes, pointer = 0;
  try {
    const engineScriptUrl = new URL('hebichess.js', self.location.href).href;
    const modelUrl = url || new URL(frozenModel.file, engineScriptUrl).href;
    const expected = (expectedSha256 || frozenModel.sha256).toLowerCase();
    diagnostic('NNUE download started', {modelUrl});
    const response = await fetch(modelUrl, {cache:'default'});
    if (!response.ok) throw Error(`NNUE download failed: HTTP ${response.status}`);
    bytes = new Uint8Array(await response.arrayBuffer());
    const actual = await sha256(bytes);
    if (actual !== expected) throw Error(`NNUE SHA-256 mismatch: expected ${expected}, got ${actual}`);
    pointer = module._malloc(bytes.byteLength);
    if (!pointer) throw Error('WASM could not allocate NNUE transfer buffer');
    module.HEAPU8.set(bytes, pointer);
    if (!module.ccall('hebichess_nnue_load_bytes', 'number', ['number','number'], [pointer, bytes.byteLength])) {
      throw Error(module.ccall('hebichess_take_output', 'string', [], []).trim() || 'NNUE parser rejected model');
    }
    nnueState = 'nnue-ready';
    diagnostic('NNUE loaded', {modelUrl, bytes:bytes.byteLength, sha256:actual});
    publishNnue({modelUrl, bytes:bytes.byteLength, sha256:actual});
  } catch (error) {
    nnueState = 'nnue-load-failed';
    publishNnue({error:String(error.message || error)});
  } finally {
    if (pointer) module._free(pointer);
    // The fetched ArrayBuffer and the linear-memory transfer allocation are
    // both released after the C++ Network has copied its resident tensors.
    bytes = null;
  }
}

async function loadBook({url, sha256: expectedSha256, seed} = {}) {
  bookState = 'book-loading'; publishBook();
  let bytes, pointer = 0;
  try {
    if (!url || !expectedSha256) throw Error('book load requires url and sha256');
    diagnostic('opening book download started', {bookUrl:url});
    const response = await fetch(url, {cache:'default'});
    if (!response.ok) throw Error(`opening book download failed: HTTP ${response.status}`);
    bytes = new Uint8Array(await response.arrayBuffer());
    const expected = String(expectedSha256).toLowerCase();
    const actual = await sha256(bytes);
    if (actual !== expected) throw Error(`opening book SHA-256 mismatch: expected ${expected}, got ${actual}`);
    pointer = module._malloc(bytes.byteLength);
    if (!pointer) throw Error('WASM could not allocate opening book transfer buffer');
    module.HEAPU8.set(bytes, pointer);
    if (!module.ccall('hebichess_book_load_bytes', 'number', ['number','number'], [pointer, bytes.byteLength])) {
      throw Error(module.ccall('hebichess_take_output', 'string', [], []).trim() || 'opening book parser rejected book');
    }
    if (seed !== undefined && seed !== null) command(`setoption name BookSeed value ${String(seed)}`, {});
    command('setoption name OwnBook value true', {});
    bookState = 'book-ready';
    diagnostic('opening book loaded', {bookUrl:url, bytes:bytes.byteLength, sha256:actual});
    publishBook({url, bytes:bytes.byteLength, sha256:actual});
  } catch (error) {
    command('setoption name OwnBook value false', {});
    bookState = 'book-load-failed';
    publishBook({error:String(error.message || error)});
  } finally {
    if (pointer) module._free(pointer);
    bytes = null;
  }
}

function setEvalMode(mode) {
  if (mode !== 'HCE' && mode !== 'NNUE') throw Error(`unsupported EvalMode ${mode}`);
  if (mode === 'NNUE' && nnueState !== 'nnue-ready') {
    throw Error(`EvalMode NNUE requested while state is ${nnueState}; load and wait for NNUE first`);
  }
  command(`setoption name EvalMode value ${mode}`, {});
  evalMode = mode;
  self.postMessage({type:'eval-mode', mode, nnueState});
}

self.onmessage = async ({data:message = {}}) => {
  try {
    if (message.type === 'init') { await initialize(); diagnostic('worker ready'); self.postMessage({type:'ready', nnueState, evalMode}); return; }
    if (!module) throw Error('engine is not initialized');
    if (message.type === 'load-nnue') { await loadNnue(message); return; }
    if (message.type === 'load-book') { await loadBook(message); return; }
    if (message.type === 'set-eval-mode') { setEvalMode(String(message.mode || '')); return; }
    if (message.type === 'nnue-evaluate') {
      if (nnueState !== 'nnue-ready') throw Error(`NNUE raw evaluation requested while state is ${nnueState}`);
      const value = module.ccall('hebichess_nnue_evaluate_fen_raw', 'string', ['string'], [String(message.fen || '')]);
      if (value.startsWith('error ')) throw Error(value);
      self.postMessage({type:'nnue-evaluation', requestId:message.requestId, value:Number(value)});
      return;
    }
    if (message.type === 'position') {
      const base = message.fen ? `position fen ${message.fen}` : 'position startpos';
      command(message.moves?.length ? `${base} moves ${message.moves.join(' ')}` : base, {gameId:message.gameId});
    } else if (message.type === 'game-reset') {
      module.ccall('hebichess_game_reset', null, [], []);
      self.postMessage({type:'game-state', action:'reset', fen:module.ccall('hebichess_game_fen','string',[],[]), legalMoves:JSON.parse(module.ccall('hebichess_game_legal_moves','string',[],[]))});
    } else if (message.type === 'game-load-fen') {
      const ok=module.ccall('hebichess_game_load_fen','number',['string'],[message.fen]);
      self.postMessage({type:'game-state', action:'load-fen', ok:Boolean(ok), fen:ok?module.ccall('hebichess_game_fen','string',[],[]):null, legalMoves:ok?JSON.parse(module.ccall('hebichess_game_legal_moves','string',[],[])):[]});
    } else if (message.type === 'game-legal-moves') {
      self.postMessage({type:'game-state', action:'legal-moves', legalMoves:JSON.parse(module.ccall('hebichess_game_legal_moves','string',[],[]))});
    } else if (message.type === 'game-apply') {
      const result=JSON.parse(module.ccall('hebichess_game_apply','string',['string'],[message.move]));
      self.postMessage({type:'game-state', action:'apply', ...result, legalMoves:result.ok?JSON.parse(module.ccall('hebichess_game_legal_moves','string',[],[])):[]});
    } else if (message.type === 'go') {
      if (evalMode === 'NNUE' && nnueState !== 'nnue-ready') throw Error('NNUE search blocked: model is not ready');
      const limits = Number.isFinite(Number(message.depth)) ? `depth ${Math.max(1, Number(message.depth))}` : `movetime ${Math.max(1, Number(message.movetime) || 1)}`;
      command(`go ${limits}`, {gameId:message.gameId, searchId:message.searchId});
    } else if (message.type === 'command') {
      const text = String(message.command || '');
      if (/^setoption name EvalMode value (HCE|NNUE)$/i.test(text)) setEvalMode(text.slice(text.lastIndexOf(' ') + 1).toUpperCase());
      else if (/^setoption name EvalFile value /i.test(text)) throw Error('browser EvalFile uses loadNnue({url, sha256}), not host paths');
      else command(text, {gameId:message.gameId, searchId:message.searchId});
    } else throw Error(`unsupported worker message: ${message.type}`);
  } catch (error) { self.postMessage({type:'error', message:String(error.message || error), gameId:message.gameId, searchId:message.searchId, requestId:message.requestId}); }
};
