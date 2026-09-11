/* Synthetic Phase 3 relay benchmark: browser snapshots are precomputed. */
const {performance} = require('node:perf_hooks');
const {start, currentState, getGames} = require('./server');
const games = Number(process.argv[2] || 20), plies = Number(process.argv[3] || 40);
const started = Array.from({length:games},(_,i)=>start(`benchmark-${i}`,'white'));
const t0 = performance.now();
for(let ply=0;ply<plies;++ply) for(const target of started){const state=currentState(target.playerSessionId,target.id);target.snapshot={...target.snapshot,moves:[...state.moves,`synthetic-${ply}`],san:[...state.san,`S${ply}`]};target.revision++;}
console.log(JSON.stringify({games,plies,updates:games*plies,wallMs:Number((performance.now()-t0).toFixed(3)),serverCpuMs:null,eventFanout:getGames().size,authority:'browser-wasm'}));
