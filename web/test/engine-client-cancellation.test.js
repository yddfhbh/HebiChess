const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');

const source=fs.readFileSync('public/engine/engine-client.js','utf8');

class FakeWorker {
  static instances=[];
  constructor() { this.messages=[]; this.terminated=false; FakeWorker.instances.push(this); }
  postMessage(message) { this.messages.push(message); }
  terminate() { this.terminated=true; }
  send(message) { this.onmessage?.({data:message}); }
}

async function client() {
  FakeWorker.instances=[];
  const context={self:{},Worker:FakeWorker,performance:{now:()=>0}};
  vm.runInNewContext(source,context);
  const engine=new context.self.HebiChessEngineClient();
  const ready=engine.initialize();
  const worker=FakeWorker.instances[0];
  worker.send({type:'ready'});
  await ready;
  engine.position();
  return {engine,worker};
}

test('superseded searches and position changes reject the old Promise',async()=>{
  const {engine}=await client();
  const first=engine.go({depth:4});
  const second=engine.go({depth:5});
  await assert.rejects(first,error=>error.name==='AbortError');
  engine.position();
  await assert.rejects(second,error=>error.name==='AbortError');
  engine.terminate();
});

test('terminate rejects a pending search and stale results cannot resolve a newer search',async()=>{
  const {engine,worker}=await client();
  const old=engine.go({depth:4});
  const current=engine.go({depth:5});
  await assert.rejects(old,error=>error.name==='AbortError');
  worker.send({type:'bestmove',gameId:engine.gameId,searchId:1,move:'a1a1'});
  let settled=false;
  current.then(()=>{settled=true;});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(settled,false);
  worker.send({type:'bestmove',gameId:engine.gameId,searchId:engine.searchId,move:'e2e4'});
  assert.equal(await current,'e2e4');
  const pending=engine.go({depth:6});
  engine.terminate();
  await assert.rejects(pending,error=>error.name==='AbortError');
});

test('reinitializing the worker rejects its pending search',async()=>{
  const {engine}=await client();
  const pending=engine.go({depth:4});
  const ready=engine.initialize();
  const replacement=FakeWorker.instances[1];
  replacement.send({type:'ready'});
  await assert.rejects(pending,error=>error.name==='AbortError');
  await ready;
  engine.terminate();
});
