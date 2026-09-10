(function(root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.HebiChessEngineTelemetry = api;
})(typeof globalThis === 'undefined' ? this : globalThis, function() {
  const copy = info => info ? {...info} : null;

  function create() {
    let gameId = null;
    let currentSearch = null;
    let currentSearchInfo = null;
    let lastSearchInfo = null;

    const belongsToCurrentSearch = search => currentSearch &&
      search?.gameId === currentSearch.gameId && search?.searchId === currentSearch.searchId;

    return {
      reset(nextGameId = null) {
        gameId = nextGameId;
        currentSearch = null;
        currentSearchInfo = null;
        lastSearchInfo = null;
      },
      // Repeated server snapshots, including terminal SSE, stay in one game.
      setGame(nextGameId = null) {
        if (nextGameId === gameId) return false;
        this.reset(nextGameId);
        return true;
      },
      // Replacing a Worker must not erase telemetry for the same game.
      newSession(nextGameId = gameId) {
        return this.setGame(nextGameId);
      },
      start(search) {
        if (search?.gameId !== gameId) return false;
        currentSearch = {...search};
        currentSearchInfo = null;
        return true;
      },
      update(search, info) {
        if (!belongsToCurrentSearch(search)) return false;
        currentSearchInfo = {...(currentSearchInfo || {}), ...info};
        return true;
      },
      complete(search) {
        if (!belongsToCurrentSearch(search)) return false;
        lastSearchInfo = copy(currentSearchInfo);
        currentSearch = null;
        currentSearchInfo = null;
        return true;
      },
      current() { return copy(currentSearchInfo); },
      last() { return copy(lastSearchInfo); },
      game() { return gameId; }
    };
  }
  return {create};
});
