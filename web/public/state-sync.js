(function(root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.HebiChessStateSync = api;
})(typeof globalThis === 'undefined' ? this : globalThis, function() {
  function shouldApplyServerState(current, next) {
    if (!next || typeof next !== 'object') return false;
    if (!current || !current.gameId) return true;
    if (!next.gameId) return false;
    if (next.gameId !== current.gameId) return true;
    if (current.result && !next.result) return false;
    if (!Number.isInteger(current.revision) || !Number.isInteger(next.revision)) return true;
    return next.revision >= current.revision;
  }
  function createController({getState, setState, render, reconcile, debug = false}) {
    return {
      apply(next, source = 'unknown') {
        const current = getState();
        const applied = shouldApplyServerState(current, next);
        if (debug) console.debug?.('[Phase2][state applied/ignored]', {source, gameId:next?.gameId ?? null, incomingRevision:next?.revision ?? null, currentRevision:current?.revision ?? null, turn:next?.turn ?? null, status:next?.result ?? null, applied});
        if (!applied) return false;
        setState(next);
        render();
        reconcile();
        return true;
      }
    };
  }
  return {shouldApplyServerState, createController};
});
