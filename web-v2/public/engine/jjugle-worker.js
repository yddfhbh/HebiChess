/* JJUGLE's browser adapter intentionally exposes the small UCI surface used by app.js. */
/* global importScripts */
const MODEL = {
  file: 'models/hebinnue-v3-4c815d54bc6c9fbf.hebinnue',
  sha256: '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826'
};
const BOOK = {
  file: 'books/witty_alien-v1-0edeb48aeb553d16.hebibook',
  sha256: '0edeb48aeb553d16fb79af5de01e43f422ee8c162b14f73b40b27972cb4c50a9',
  bytes: 1889392
};
const ENGINE_ASSET_VERSION = 'v2';
let modulePromise = null;
let engine = null;
let bootPromise = null;
let bootError = null;

function line(text) { self.postMessage(String(text)); }
function error(message) { line(`info string error ${message}`); self.postMessage({ type: 'error', message: String(message) }); }
function emitBuffer() {
  if (!engine) return;
  const output = engine.ccall('hebichess_take_output', 'string', [], []) || '';
  for (const value of output.split(/\r?\n/)) if (value) line(value);
}
function hex(bytes) { return [...new Uint8Array(bytes)].map(value => value.toString(16).padStart(2, '0')).join(''); }
async function digest(bytes) {
  if (!self.crypto?.subtle) throw new Error('SHA-256 requires a secure context or localhost');
  return hex(await self.crypto.subtle.digest('SHA-256', bytes));
}
async function fetchBytes(file, expected, expectedBytes) {
  const url = new URL(file, self.location.href);
  const response = await fetch(url, { cache: 'default' });
  if (!response.ok) throw new Error(`${file} download failed (HTTP ${response.status})`);
  const bytes = await response.arrayBuffer();
  if (expectedBytes !== undefined && bytes.byteLength !== expectedBytes) throw new Error(`${file} size mismatch (expected ${expectedBytes}, got ${bytes.byteLength})`);
  const actual = await digest(bytes);
  if (actual !== expected) throw new Error(`${file} SHA-256 mismatch (expected ${expected}, got ${actual})`);
  return new Uint8Array(bytes);
}
async function loadBytes(bytes, functionName, label) {
  const pointer = engine._malloc(bytes.byteLength);
  if (!pointer) throw new Error(`WASM could not allocate ${label} buffer`);
  try {
    engine.HEAPU8.set(bytes, pointer);
    if (!engine.ccall(functionName, 'number', ['number', 'number'], [pointer, bytes.byteLength])) {
      emitBuffer(); throw new Error(`${label} parser rejected the binary`);
    }
  } finally { engine._free(pointer); }
}
async function boot() {
  const script = new URL(`hebichess.js?${ENGINE_ASSET_VERSION}`, self.location.href);
  importScripts(script.href);
  modulePromise = self.createHebiChessModule({ locateFile: file => new URL(`${file}?${ENGINE_ASSET_VERSION}`, script.href).href });
  engine = await modulePromise;
  self.hebichessOutputLine = value => line(value);
  // The engine is never advertised as NNUE until the verified model is resident.
  const model = await fetchBytes(MODEL.file, MODEL.sha256);
  await loadBytes(model, 'hebichess_nnue_load_bytes', 'NNUE model');
  engine.ccall('hebichess_send_command', null, ['string'], ['setoption name EvalMode value NNUE']);
  const book = await fetchBytes(BOOK.file, BOOK.sha256, BOOK.bytes);
  await loadBytes(book, 'hebichess_book_load_bytes', 'opening book');
  engine.ccall('hebichess_send_command', null, ['string'], ['setoption name OwnBook value true']);
  engine.ccall('hebichess_send_command', null, ['string'], ['setoption name MaxMoveTime value 20000']);
  engine.ccall('hebichess_send_command', null, ['string'], ['uci']);
  engine.ccall('hebichess_send_command', null, ['string'], ['isready']);
}
function ensureBoot() {
  if (!bootPromise) bootPromise = boot().catch(errorValue => { bootError = errorValue; throw errorValue; });
  return bootPromise;
}
function command(text) {
  engine.ccall('hebichess_send_command', null, ['string'], [text]);
}
self.onmessage = ({ data }) => {
  const text = typeof data === 'string' ? data.trim() : '';
  if (!text) return;
  if (text === 'uci') {
    ensureBoot().catch(value => error(value.message));
    return;
  }
  ensureBoot().then(() => {
    if (text === 'stop') command('stop');
    else if (/^(isready|ucinewgame|position |go |setoption )/.test(text)) command(text);
    else error(`unsupported UCI command: ${text}`);
  }).catch(value => error(bootError?.message || value.message));
};
