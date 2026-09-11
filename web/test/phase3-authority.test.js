const test=require('node:test');const assert=require('node:assert/strict');const {spawn}=require('node:child_process');const os=require('node:os');const path=require('node:path');const fs=require('node:fs');
const port=3437,data=path.join(os.tmpdir(),`hebichess-phase3-${process.pid}.json`);let child;
test.before(async()=>{child=spawn(process.execPath,['server.js'],{cwd:__dirname+'/..',env:{...process.env,PORT:String(port),DATA_PATH:data}});await new Promise((resolve,reject)=>{child.stdout.on('data',d=>{if(String(d).includes('listening'))resolve()});child.on('error',reject)})});test.after(()=>{child.kill();try{fs.unlinkSync(data)}catch{}});
async function call(url,body,cookie){const r=await fetch(`http://127.0.0.1:${port}${url}`,{method:body?'POST':'GET',headers:{'content-type':'application/json',...(cookie?{cookie}:{})},body:body&&JSON.stringify(body)});return {status:r.status,data:await r.json(),cookie:r.headers.get('set-cookie')?.split(';')[0]||cookie};}
const snap=(result=null)=>({fen:'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1',moves:['e2e4'],san:['e4'],result,termination:result?'checkmate':null,lastMove:'e2e4',legalMoves:[]});
test('live selection reads only hydrated browser GameState legal moves',()=>{const app=fs.readFileSync(path.join(__dirname,'..','public','app.js'),'utf8');const interaction=fs.readFileSync(path.join(__dirname,'..','public','interaction-fixes.js'),'utf8');const server=fs.readFileSync(path.join(__dirname,'..','server.js'),'utf8');assert.match(app,/browserGameState\.legalMoves/);assert.match(app,/!browserGameState\.hydrated/);assert.match(app,/client\.gameLoadFen\(state\.currentFen\)/);assert.doesNotMatch(app,/normalTargets\(from\).*S\.legalMoves/);assert.doesNotMatch(interaction,/legalMoves: S\.legalMoves/);assert.doesNotMatch(server,/legalMoves:s\.legalMoves/);assert.match(server,/legalMoves: _serverLegalMoves/)});
test('browser snapshot authority, exact revision, ownership, terminal rejection and explicit spectator selection',async()=>{const a='hebichess_session=phase3-a',b='hebichess_session=phase3-b';const A=await call('/api/start',{color:'white'},a),B=await call('/api/state',null,b);assert.equal(A.status,200);assert.equal(B.data.active,false);const game=A.data.gameId;assert.equal((await call('/api/move',{gameId:game,revision:0,snapshot:snap()},b)).status,403);assert.equal((await call('/api/move',{gameId:game,revision:9,snapshot:snap()},a)).status,409);const moved=await call('/api/move',{gameId:game,revision:0,snapshot:snap()},a);assert.equal(moved.status,200);assert.equal(moved.data.moves[0],'e2e4');const spectator=await call(`/api/state?gameId=${game}`,null,b);assert.equal(spectator.data.role,'spectator');assert.equal((await call('/api/move',{gameId:game,revision:1,snapshot:snap()},b)).status,403);const terminal=await call('/api/engine-move',{gameId:game,revision:1,snapshot:snap('1-0')},a);assert.equal(terminal.status,200);assert.equal((await call('/api/move',{gameId:game,revision:2,snapshot:snap()},a)).status,409);assert.equal((await call('/api/resign',{gameId:game,revision:2},a)).status,409);});

test('resign is a server command with exact revision, persistence and ownership release',async()=>{
  const a='hebichess_session=resign-owner', b='hebichess_session=resign-spectator';
  const started=await call('/api/start',{color:'white'},a), game=started.data.gameId;
  assert.equal((await call('/api/resign',{gameId:game,revision:0},b)).status,403);
  assert.equal((await call('/api/resign',{gameId:game,revision:9},a)).status,409);
  const resigned=await call('/api/resign',{gameId:game,revision:0},a);
  assert.equal(resigned.status,200);
  assert.equal(resigned.data.state.active,false);
  assert.equal(resigned.data.state.result,'0-1');
  assert.equal(resigned.data.state.termination,'resign');
  assert.equal(resigned.data.state.engineThinking,false);
  assert.equal(resigned.data.state.revision,1);
  assert.equal((await call('/api/resign',{gameId:game,revision:1},a)).status,409);
  const replacement=await call('/api/start',{color:'black'},a);
  assert.equal(replacement.status,200);
  assert.notEqual(replacement.data.gameId,game);
  const history=JSON.parse(fs.readFileSync(data));
  assert.equal(history.at(-1).termination,'resign');
});
