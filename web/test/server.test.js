const test = require('node:test');
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const port = 3417;
const testData = path.join(os.tmpdir(), `hebichess-web-test-${process.pid}.json`);
let child;
test.before(async()=>{child=spawn(process.execPath,['server.js'],{cwd:__dirname+'/..',env:{...process.env,PORT:String(port),HEBICHESS_BINARY:'../build/HebiChess',DEFAULT_SEARCH_MOVETIME_MS:'1',DATA_PATH:testData}});await new Promise((resolve,reject)=>{child.stdout.on('data',d=>{if(d.toString().includes('listening'))resolve()});child.on('error',reject)})});
test.after(()=>{child.kill();try{fs.unlinkSync(testData)}catch(error){if(error.code!=='ENOENT')throw error}});
async function call(path,body,cookie){const r=await fetch(`http://127.0.0.1:${port}${path}`,{method:body?'POST':'GET',headers:{'content-type':'application/json',...(cookie?{cookie}: {})},body:body&&JSON.stringify(body)});const raw=await r.text();let data;try{data=JSON.parse(raw)}catch{data=raw}return {status:r.status,data,cookie:r.headers.get('set-cookie')?.split(';')[0]||cookie}}
function session(value){return `hebichess_session=${value}`}
function sse(cookie){return fetch(`http://127.0.0.1:${port}/events`,{headers:{cookie}})}
async function firstEvent(response){const reader=response.body.getReader();let text='';for(;;){const {value,done}=await reader.read();if(done)throw Error('SSE closed');text+=Buffer.from(value).toString();const match=text.match(/data: (.+)\n\n/);if(match){reader.releaseLock();return JSON.parse(match[1])}}}
test('session-personalized player, lobby fallback and persistent SSE role',async()=>{let a=await call('/');assert.equal(a.status,200);assert.match(String(a.data),/<!doctype html/i);assert.match(a.cookie,/^hebichess_session=/);const ac=a.cookie,bc=session('spectator-b');a=await call('/api/state',null,ac);assert.equal(a.data.active,false);const streamA=await sse(ac);assert.equal((await firstEvent(streamA)).active,false);a=await call('/api/start',{color:'white'},ac);assert.equal(a.status,200);assert.equal(a.data.role,'player');assert.equal(a.data.isPlayer,true);assert.equal(a.data.revision,0);assert.ok(a.data.gameId);const gameId=a.data.gameId;let b=await call('/api/state',null,bc);assert.equal(b.data.role,'lobby');assert.equal(b.data.active,false);b=await call('/api/move',{gameId,revision:0,snapshot:{fen:'x',moves:[],san:[]}},bc);assert.equal(b.status,403);b=await call('/api/move',{gameId,revision:0},ac);assert.equal(b.status,422);b=await call('/api/move',{gameId,revision:a.data.revision,snapshot:{fen:'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1',moves:['e2e4'],san:['e4']}},ac);assert.equal(b.status,200);assert.equal(b.data.revision,1);const refreshed=await call('/api/state?gameId='+gameId,null,ac);assert.equal(refreshed.data.role,'player');assert.equal(refreshed.data.isPlayer,true);b=await call('/api/resign',{gameId,revision:b.data.revision},ac);assert.equal(b.status,200);assert.equal(b.data.state.gameId,gameId);assert.equal((await call('/api/state?gameId='+gameId,null,ac)).data.active,false);assert.equal((await call('/api/state?gameId='+gameId,null,bc)).data.active,false);});
test('SSE connected before start receives player role',async()=>{const ac=session('player-sse');const stream=await sse(ac);assert.equal((await firstEvent(stream)).active,false);const started=await call('/api/start',{color:'white'},ac);assert.equal(started.status,200);const event=await firstEvent(stream);assert.equal(event.role,'player');assert.equal(event.isPlayer,true);await call('/api/resign',{gameId:started.data.gameId,revision:0},ac);});
test('two owned games are isolated and enforce revision and engine trust boundary',async()=>{
  const a=session('concurrent-a'), b=session('concurrent-b');
  const startedA=await call('/api/start',{color:'white'},a), startedB=await call('/api/start',{color:'white'},b);
  assert.equal(startedA.status,200); assert.equal(startedB.status,200); assert.notEqual(startedA.data.gameId,startedB.data.gameId);
  const gameA=startedA.data.gameId, gameB=startedB.data.gameId;
  let moveA=await call('/api/move',{gameId:gameA,revision:0,snapshot:{fen:'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1',moves:['e2e4'],san:['e4']}},a), moveB=await call('/api/move',{gameId:gameB,revision:0,snapshot:{fen:'rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1',moves:['d2d4'],san:['d4']}},b);
  assert.equal(moveA.status,200); assert.equal(moveB.status,200); assert.equal(moveA.data.revision,1); assert.equal(moveB.data.revision,1);
  assert.equal((await call('/api/state?gameId='+gameA,null,b)).data.moves.join(','),'e2e4');
  assert.equal((await call('/api/state?gameId='+gameB,null,a)).data.moves.join(','),'d2d4');
  assert.equal((await call('/api/move',{gameId:gameA,revision:0,move:'e2e3'},a)).status,409);
  assert.equal((await call('/api/move',{gameId:gameA,revision:1,move:'e7e5'},a)).status,409);
  assert.equal((await call('/api/engine-move',{gameId:gameA,revision:1,snapshot:{fen:'rnbqkbnr/pppppppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1',moves:['e2e4','e7e6'],san:['e4','e6']}},b)).status,403);
  assert.equal((await call('/api/engine-move',{gameId:gameA,revision:1,snapshot:{fen:'rnbqkbnr/pppppppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1',moves:['e2e4','e7e6'],san:['e4','e6']}},a)).status,200);
  assert.equal((await call('/api/engine-move',{gameId:gameA,revision:1,move:'d7d5'},a)).status,409);
  assert.equal((await call('/api/move',{gameId:gameA,revision:2,move:'e2e4'},a)).status,422);
  assert.equal((await call('/api/resign',{gameId:gameA,revision:2},a)).status,200);
  assert.equal((await call('/api/resign',{gameId:gameB,revision:1},b)).status,200);
  const history=JSON.parse(fs.readFileSync(testData)); assert.ok(history.filter(item=>item.moves.length>=1).length>=2);
});
test('black-owned game starts on the engine turn without a native process',async()=>{
  const owner=session('black-owner'), started=await call('/api/start',{color:'black'},owner);
  assert.equal(started.status,200); assert.equal(started.data.engineThinking,true); assert.equal(started.data.turn,'w');
  assert.doesNotMatch(fs.readFileSync(path.join(__dirname,'..','server.js'),'utf8'),/\bspawn\s*\(/);
  assert.equal((await call('/api/engine-move',{gameId:started.data.gameId,revision:0,snapshot:{fen:'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1',moves:['e2e4'],san:['e4']}},owner)).status,200);
  const resigned=await call('/api/resign',{gameId:started.data.gameId,revision:1},owner); assert.equal(resigned.status,200); assert.equal(resigned.data.state.revision,2);
});
