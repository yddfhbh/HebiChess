/* Keep every game mutation on the revisioned API contract. */
(function (global, factory) {
  const mutation = factory();
  global.HebiChessMutation = mutation;
  if (typeof module !== 'undefined') module.exports = mutation;
})(typeof self !== 'undefined' ? self : globalThis, function () {
  function revisionedPayload(state, extra = {}) {
    if (!state?.gameId) throw Error('no active game');
    if (!Number.isInteger(state.revision)) throw Error('no current game revision');
    return {gameId: state.gameId, revision: state.revision, ...extra};
  }

  function create({api, getState}) {
    return {
      move(move) {
        return api('/api/move', revisionedPayload(getState(), {move}));
      },
      resign() {
        return api('/api/resign', revisionedPayload(getState()));
      },
      engineMove(move, snapshot) {
        return api('/api/engine-move', revisionedPayload(snapshot, {move}));
      }
    };
  }

  return {create, revisionedPayload};
});
