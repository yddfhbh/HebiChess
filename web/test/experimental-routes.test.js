const test=require('node:test');
const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const fs=require('node:fs');
const os=require('node:os');

let nextPort=3420;
async function server(production) {
  const port=nextPort++;
  const data=`${os.tmpdir()}/hebichess-route-test-${process.pid}-${port}.json`;
  const child=spawn(process.execPath,['server.js'],{cwd:__dirname+'/..',env:{...process.env,PORT:String(port),NODE_ENV:production?'production':'development',DATA_PATH:data}});
  await new Promise((resolve,reject)=>{child.stdout.on('data',data=>{if(data.toString().includes('listening'))resolve()});child.on('error',reject)});
  return {child,base:`http://127.0.0.1:${port}`,data};
}
async function get(base,path) { return fetch(`${base}${path}`); }
async function close(value) { value.child.kill(); try { fs.unlinkSync(value.data); } catch (error) { if (error.code!=='ENOENT') throw error; } }

test('experimental verification routes are available in development',async()=>{
  const value=await server(false);
  try { assert.equal((await get(value.base,'/engine-test')).status,200); assert.equal((await get(value.base,'/test-data/wasm-parity-100.fen')).status,200); }
  finally { await close(value); }
});

test('experimental verification routes are unavailable in production',async()=>{
  const value=await server(true);
  try { assert.equal((await get(value.base,'/engine-test')).status,404); assert.equal((await get(value.base,'/test-data/wasm-parity-100.fen')).status,404); }
  finally { await close(value); }
});
