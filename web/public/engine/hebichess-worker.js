/* global importScripts */
// The Worker owns all engine state.  NNUE bytes never enter the UI thread.
let modulePromise, module, liveContext = {};
let evalMode = 'HCE';
let nnueState = 'hce-ready';
let bookState = 'book-unavailable';
const ENGINE_ASSET_VERSION = '20260922jjugle1';
const frozenModel = {
  file: 'models/hebinnue-v3-4c815d54bc6c9fbf.hebinnue',
  sha256: '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826'
};
const frozenBook = {
  file: 'books/witty_alien-v1-0edeb48aeb553d16.hebibook',
  sha256: '0edeb48aeb553d16fb79af5de01e43f422ee8c162b14f73b40b27972cb4c50a9',
  bytes: 1889392
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

async function fetchWithTimeout(url, options = {}, timeoutMs = 10000) {
  const controller = typeof AbortController === 'function' ? new AbortController() : null;
  const timer = setTimeout(() => controller?.abort(), timeoutMs);
  try {
    return await fetch(url, controller ? {...options, signal:controller.signal} : options);
  } catch (error) {
    if (controller?.signal.aborted) throw Error(`opening book download timed out after ${timeoutMs}ms`);
    throw error;
  } finally {
    clearTimeout(timer);
  }
}

function automaticBookSeed() {
  try {
    if (!self.crypto?.getRandomValues) throw Error('crypto.getRandomValues unavailable');
    const words = new Uint32Array(2);
    self.crypto.getRandomValues(words);
    return ((BigInt(words[0]) << 32n) | BigInt(words[1])).toString();
  } catch (error) {
    diagnostic('opening book random seed unavailable', {error:String(error.message || error)});
    return '0';
  }
}

async function initialize() {
  diagnostic('worker initialize');
  if (!modulePromise) {
    const engineScriptUrl = new URL(`hebichess.js?v=${ENGINE_ASSET_VERSION}`, self.location.href).href;
    const engineWasmUrl = new URL('hebichess.wasm', engineScriptUrl).href;
    diagnostic('engine artifacts resolved', {engineScriptUrl, engineWasmUrl});
    importScripts(engineScriptUrl);
    modulePromise = self.createHebiChessModule({locateFile:file => `${new URL(file, engineScriptUrl).href}?v=${ENGINE_ASSET_VERSION}`});
  }
  module = await modulePromise;
  module.ccall('hebichess_initialize', null, [], []);
  diagnostic('worker engine initialized');
  emitOutput();
  publishNnue();
  await loadBook({automatic:true});
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

async function loadBook({url, sha256: expectedSha256, seed, automatic = false} = {}) {
  bookState = 'book-loading'; publishBook();
  let bytes, pointer = 0;
  try {
    const engineScriptUrl = new URL('hebichess.js', self.location.href).href;
    const bookUrl = url || (automatic ? new URL(frozenBook.file, engineScriptUrl).href : null);
    const expected = String(expectedSha256 || (automatic ? frozenBook.sha256 : '')).toLowerCase();
    if (!bookUrl || !expected) throw Error('book load requires url and sha256');
    const bookSeed = seed !== undefined && seed !== null ? String(seed) : automatic ? automaticBookSeed() : null;
    diagnostic('opening book download started', {bookUrl, expectedBytes:automatic ? frozenBook.bytes : undefined});
    const response = await fetchWithTimeout(bookUrl, {cache:'default'});
    if (!response.ok) throw Error(`opening book download failed: HTTP ${response.status}`);
    bytes = new Uint8Array(await response.arrayBuffer());
    if (automatic && bytes.byteLength !== frozenBook.bytes) throw Error(`opening book byte-size mismatch: expected ${frozenBook.bytes}, got ${bytes.byteLength}`);
    const actual = await sha256(bytes);
    if (actual !== expected) throw Error(`opening book SHA-256 mismatch: expected ${expected}, got ${actual}`);
    pointer = module._malloc(bytes.byteLength);
    if (!pointer) throw Error('WASM could not allocate opening book transfer buffer');
    module.HEAPU8.set(bytes, pointer);
    if (!module.ccall('hebichess_book_load_bytes', 'number', ['number','number'], [pointer, bytes.byteLength])) {
      throw Error(module.ccall('hebichess_take_output', 'string', [], []).trim() || 'opening book parser rejected book');
    }
    if (bookSeed !== null) command(`setoption name BookSeed value ${bookSeed}`, {});
    command('setoption name OwnBook value true', {});
    bookState = 'book-ready';
    diagnostic('opening book loaded', {bookUrl, bytes:bytes.byteLength, sha256:actual, seed:bookSeed});
    publishBook({url:bookUrl, bytes:bytes.byteLength, sha256:actual, seed:bookSeed});
  } catch (error) {
    module.ccall('hebichess_book_clear', null, [], []);
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
    if (message.type === 'init') { await initialize(); diagnostic('worker ready'); self.postMessage({type:'ready', nnueState, evalMode, bookState}); return; }
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
