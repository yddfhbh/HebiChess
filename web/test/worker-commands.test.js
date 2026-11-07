const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');

function goCommand(message) {
  const source=fs.readFileSync('public/engine/hebichess-worker.js','utf8');
  const self={postMessage(){}};
  vm.runInNewContext(source,{self,performance:{now:()=>0},URL,fetch,setTimeout,clearTimeout,AbortController,BigInt});
  return self.hebichessBuildGoCommand(message);
}

test('Worker forwards browser clock limits as UCI go fields',()=>{
  assert.equal(goCommand({wtime:60000,btime:30000,winc:1000,binc:2000}),
    'go wtime 60000 btime 30000 winc 1000 binc 2000');
  assert.equal(goCommand({wtime:0,btime:0,winc:0,binc:0}),
    'go wtime 0 btime 0 winc 0 binc 0');
});

test('Worker preserves explicit movetime/depth and only uses one-ms fallback when no limit exists',()=>{
  assert.equal(goCommand({movetime:250}), 'go movetime 250');
  assert.equal(goCommand({depth:0}), 'go depth 1');
  assert.equal(goCommand({}), 'go movetime 1');
});
