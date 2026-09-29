const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const workerSource = fs.readFileSync('public/engine/hebichess-worker.js', 'utf8');
const appSource = fs.readFileSync('public/app.js', 'utf8');
const modelSha256 = '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826';
const bookSha256 = '0edeb48aeb553d16fb79af5de01e43f422ee8c162b14f73b40b27972cb4c50a9';

function bytesFromHex(value) {
  return Uint8Array.from(value.match(/../g), byte => Number.parseInt(byte, 16));
}

function createWorkerHarness() {
  const messages = [];
  const fetches = [];
  const commands = [];
  let nnueLoads = 0;
  const module = {
    HEAPU8: new Uint8Array(1024),
    _malloc: () => 8,
    _free() {},
    ccall(name, _returnType, _argTypes, args = []) {
      if (name === 'hebichess_send_command') commands.push(args[0]);
      if (name === 'hebichess_nnue_load_bytes') { nnueLoads += 1; return 1; }
      if (name === 'hebichess_book_load_bytes') return 1;
      if (name === 'hebichess_take_output') return '';
      return 1;
    }
  };
  const self = {
    location: {href:'https://example.test/public/engine/hebichess-worker.js'},
    postMessage: message => messages.push(message),
    crypto: {
      getRandomValues(words) { words.fill(7); return words; },
      subtle: {async digest(_algorithm, input) {
        return bytesFromHex(new Uint8Array(input).byteLength === 2 ? modelSha256 : bookSha256).buffer;
      }}
    }
  };
  const context = {
    self,
    URL,
    Uint8Array,
    BigInt,
    AbortController,
    performance,
    setTimeout,
    clearTimeout,
    importScripts() { self.createHebiChessModule = async () => module; },
    async fetch(url) {
      fetches.push(String(url));
      const body = String(url).includes('/models/') ? new Uint8Array([1, 2]) : new Uint8Array([3]);
      return {ok:true, arrayBuffer:async () => body.buffer};
    }
  };
  vm.runInNewContext(workerSource, context, {filename:'hebichess-worker.js'});
  return {self, messages, fetches, commands, get nnueLoads() { return nnueLoads; }};
}

async function initialize(harness) {
  await harness.self.onmessage({data:{type:'init'}});
  return harness.messages.find(message => message.type === 'ready');
}

test('production gameplay starts the WASM engine in HCE and skips the NNUE model fetch', async () => {
  assert.match(appSource, /get\('eval'\)\?\.toUpperCase\(\)==='NNUE'\?'NNUE':'HCE'/);
  assert.match(appSource, /async function ensureSelectedEvalMode\(client\)\{if\(requestedEvalMode==='HCE'\)return;requestedNnueLoad\?\?=client\.loadNnue\(\)/);
  assert.match(appSource, /await ensureSelectedEvalMode\(client\)/);
  const harness = createWorkerHarness();
  const ready = await initialize(harness);

  assert.equal(ready.evalMode, 'HCE');
  assert.ok(harness.commands.includes('setoption name EvalMode value HCE'));
  assert.equal(harness.nnueLoads, 0);
  assert.equal(harness.fetches.filter(url => url.includes('/models/')).length, 0);
  assert.equal(harness.fetches.filter(url => url.includes('/books/')).length, 1);
});

test('explicit NNUE load and mode selection remain available through the Worker protocol', async () => {
  const harness = createWorkerHarness();
  await initialize(harness);

  await harness.self.onmessage({data:{type:'load-nnue'}});
  assert.equal(harness.nnueLoads, 1);
  assert.equal(harness.fetches.filter(url => url.includes('/models/')).length, 1);
  assert.ok(harness.messages.some(message => message.type === 'nnue-state' && message.state === 'nnue-ready'));

  await harness.self.onmessage({data:{type:'set-eval-mode', mode:'NNUE'}});
  assert.ok(harness.commands.includes('setoption name EvalMode value NNUE'));
  assert.ok(harness.messages.some(message => message.type === 'eval-mode' && message.mode === 'NNUE'));
});
