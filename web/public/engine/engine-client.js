(function(global) {
  class HebiChessEngineClient {
    constructor(workerUrl='/public/engine/hebichess-worker.js') { this.workerUrl=workerUrl; this.gameId=0; this.searchId=0; this.handlers={info(){},output(){},error(){},diagnostic(){}}; }
    async initialize() {
      this._cancelPending('engine worker replaced');
      this.worker?.terminate(); this.worker=new Worker(this.workerUrl); this.handlers.diagnostic({step:'worker created',at:performance.now()});
      return new Promise((resolve,reject) => { this.worker.onerror=e=>reject(Error(e.message || 'worker initialization failed')); this.worker.onmessage=e=>{const m=e.data;if(m.type==='ready')resolve();else this._receive(m)}; this.worker.postMessage({type:'init'}); });
    }
    onOutput(handler) { this.handlers.output=handler; } onInfo(handler) { this.handlers.info=handler; } onError(handler) { this.handlers.error=handler; } onDiagnostic(handler) { this.handlers.diagnostic=handler; }
    position({fen,moves=[]}={}) { this._cancelPending('search superseded by position'); this.gameId++; this.searchId=0; this.worker.postMessage({type:'command',gameId:this.gameId,searchId:0,command:'ucinewgame'}); this.worker.postMessage({type:'position',gameId:this.gameId,fen,moves}); return this.gameId; }
    sendCommand(command) { this.worker.postMessage({type:'command',gameId:this.gameId,searchId:this.searchId,command}); }
    go(limits) { this._cancelPending('search superseded by newer search'); const gameId=this.gameId,searchId=++this.searchId; const request=typeof limits==='number'?{movetime:limits}:({...limits}); return new Promise((resolve,reject)=>{this.pending={gameId,searchId,resolve,reject};this.worker.postMessage({type:'go',gameId,searchId,...request})}); }
    _cancelPending(reason) { if (!this.pending) return; const pending=this.pending; this.pending=null; const error=Error(reason); error.name='AbortError'; pending.reject(error); }
    _receive(message) {
      if (message.type==='diagnostic') { this.handlers.diagnostic(message); return; }
      if (message.type==='error') { this.handlers.error(message); if(this.pending?.gameId===message.gameId&&this.pending?.searchId===message.searchId){const pending=this.pending;this.pending=null;pending.reject(Error(message.message));} return; }
      if (message.gameId != null && (message.gameId !== this.gameId || (message.searchId != null && message.searchId !== this.searchId))) { this.handlers.diagnostic({step:'stale message discarded',message,at:performance.now()}); return; }
      if (message.type==='bestmove' && this.pending) { this.pending.resolve(message.move); this.pending=null; }
      else if (message.type==='info') this.handlers.info(message); else this.handlers.output(message);
    }
    terminate() { this._cancelPending('engine terminated'); this.worker?.terminate(); this.worker=null; }
  }
  global.HebiChessEngineClient=HebiChessEngineClient;
})(self);
