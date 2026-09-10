const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const worker=fs.readFileSync('public/engine/hebichess-worker.js','utf8');
const client=fs.readFileSync('public/engine/engine-client.js','utf8');
const harness=fs.readFileSync('public/engine-test.html','utf8');
test('experimental engine uses a Worker with game/search generations',()=>{assert.match(worker,/importScripts\('\/public\/engine\/hebichess\.js'\)/);assert.match(worker,/gameId/);assert.match(worker,/searchId/);assert.match(worker,/type:'bestmove'/);assert.match(client,/new Worker/);assert.match(client,/message\.gameId !== this\.gameId/);assert.match(client,/message\.searchId !== this\.searchId/)});
test('engine harness covers startpos, FEN, move history, errors and worker recreation',()=>{assert.match(harness,/r1bqkbnr/);assert.match(harness,/e2e4','e7e5/);assert.match(harness,/engine\.go\(150\)/);assert.match(harness,/invalid-command/);assert.match(client,/sendCommand\(command\)/);assert.match(client,/this\.worker\?\.terminate\(\)/)});
