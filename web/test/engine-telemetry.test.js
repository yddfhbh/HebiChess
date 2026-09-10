const test=require('node:test');
const assert=require('node:assert/strict');
const {create}=require('../public/engine/engine-telemetry.js');

function search(telemetry, gameId='game', searchId=1) {
  telemetry.setGame(gameId);
  telemetry.start({gameId,searchId});
  return {gameId,searchId};
}

test('A: cp to mate replaces only the score and never creates 0/D0',()=>{
  const telemetry=create(), key=search(telemetry);
  telemetry.update(key,{score:{type:'cp',value:-1170},depth:8,nodes:100});
  assert.deepEqual(telemetry.current(),{score:{type:'cp',value:-1170},depth:8,nodes:100});
  telemetry.update(key,{score:{type:'mate',value:-3},depth:9});
  assert.deepEqual(telemetry.current(),{score:{type:'mate',value:-3},depth:9,nodes:100});
  assert.notEqual(telemetry.current().score.value,0);
});

test('B: mate -3 to -2 to -1 updates naturally',()=>{
  const telemetry=create(), key=search(telemetry);
  for (const [value,depth] of [[-3,9],[-2,10],[-1,12]]) {
    telemetry.update(key,{score:{type:'mate',value},depth});
    assert.deepEqual(telemetry.current().score,{type:'mate',value});
    assert.equal(telemetry.current().depth,depth);
  }
});

test('C: bestmove and terminal preserve the last mate snapshot',()=>{
  const telemetry=create(), key=search(telemetry);
  telemetry.update(key,{score:{type:'mate',value:-1},depth:12,nodes:9001});
  telemetry.complete(key);
  telemetry.setGame('game');
  assert.deepEqual(telemetry.last(),{score:{type:'mate',value:-1},depth:12,nodes:9001});
  assert.equal(telemetry.current(),null);
});

test('D: mate info has no cp/evaluation fallback',()=>{
  const telemetry=create(), key=search(telemetry);
  telemetry.update(key,{score:{type:'mate',value:-3},depth:9});
  assert.equal('evaluation' in telemetry.current(),false);
  assert.equal(telemetry.current().score.type,'mate');
});

test('E: same-game terminal/server snapshots do not clear browser telemetry',()=>{
  const telemetry=create(), key=search(telemetry,'terminal-game');
  telemetry.update(key,{score:{type:'mate',value:-1},depth:12});
  telemetry.complete(key);
  telemetry.setGame('terminal-game');
  assert.deepEqual(telemetry.last().score,{type:'mate',value:-1});
  assert.equal(telemetry.last().depth,12);
});

test('F: changing gameId is the only game telemetry reset boundary',()=>{
  const telemetry=create(), key=search(telemetry,'old-game');
  telemetry.update(key,{score:{type:'cp',value:200},depth:5});
  telemetry.complete(key);
  assert.equal(telemetry.setGame('old-game'),false);
  assert.notEqual(telemetry.last(),null);
  assert.equal(telemetry.setGame('new-game'),true);
  assert.equal(telemetry.last(),null);
});

test('stale search info cannot overwrite the completed result',()=>{
  const telemetry=create(), key=search(telemetry,'stale-game',2);
  telemetry.update(key,{score:{type:'cp',value:420},depth:10});
  telemetry.complete(key);
  assert.equal(telemetry.update({gameId:'stale-game',searchId:1},{score:{type:'mate',value:-4},depth:99}),false);
  assert.deepEqual(telemetry.last().score,{type:'cp',value:420});
});
