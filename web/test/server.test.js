const test = require('node:test');
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const port = 3417;
let child;
test.before(async()=>{child=spawn(process.execPath,['server.js'],{cwd:__dirname+'/..',env:{...process.env,PORT:String(port),HEBICHESS_BINARY:'../build/HebiChess',DEFAULT_SEARCH_DEPTH:'1'}});await new Promise((resolve,reject)=>{child.stdout.on('data',d=>{if(d.toString().includes('listening'))resolve()});child.on('error',reject)})});
test.after(()=>child.kill());
async function call(path,body,cookie){const r=await fetch(`http://127.0.0.1:${port}${path}`,{method:body?'POST':'GET',headers:{'content-type':'application/json',...(cookie?{cookie}: {})},body:body&&JSON.stringify(body)});return {status:r.status,data:await r.json(),cookie:r.headers.get('set-cookie')?.split(';')[0]||cookie}}
test('single active game ownership and server validation',async()=>{let a=await call('/api/state');assert.equal(a.data.active,false);a=await call('/api/start',{color:'white'});assert.equal(a.status,200);const ac=a.cookie;let b=await call('/api/start',{color:'black'},'hebichess_session=spectator');assert.equal(b.status,409);b=await call('/api/move',{move:'e7e5'},'hebichess_session=spectator');assert.equal(b.status,403);b=await call('/api/move',{move:'e2e5'},ac);assert.equal(b.status,422);b=await call('/api/move',{move:'e2e4'},ac);assert.equal(b.status,200);assert.deepEqual(b.data.moves,['e2e4']);});
