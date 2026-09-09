const HebiChessUi = (() => {
  const pieceValue = {q:9, r:5, b:3, n:3, p:1};

  function coordinateLabels(flipped) {
    return {
      files: (flipped ? 'hgfedcba' : 'abcdefgh').split(''),
      ranks: (flipped ? '12345678' : '87654321').split('')
    };
  }

  function materialDifference(capturedPieces = {}) {
    const total = pieces => (pieces || []).reduce((sum, piece) => sum + (pieceValue[piece] || 0), 0);
    return total(capturedPieces.w) - total(capturedPieces.b);
  }

  function squareFromClientPoint(clientX, clientY, rect, flipped = false) {
    if (!rect) return null;
    const left = Number(rect.left), top = Number(rect.top), width = Number(rect.width), height = Number(rect.height);
    if (![left, top, width, height, clientX, clientY].every(Number.isFinite) || width <= 0 || height <= 0) return null;
    const x = clientX - left, y = clientY - top;
    if (x < 0 || y < 0 || x >= width || y >= height) return null;
    const visualCol = Math.min(7, Math.floor(x * 8 / width));
    const visualRow = Math.min(7, Math.floor(y * 8 / height));
    const boardCol = flipped ? 7 - visualCol : visualCol;
    const boardRow = flipped ? 7 - visualRow : visualRow;
    return 'abcdefgh'[boardCol] + String(8 - boardRow);
  }

  function reconcilePartialPremove({mode, from, hasQueuedPremove = false, playerTurn = false, sourceIsOwnPiece = false, legalMoves = []} = {}) {
    if (mode !== 'premove' || !from || hasQueuedPremove || !playerTurn) return {action:'keep'};
    if (!sourceIsOwnPiece) return {action:'clear'};
    const legalDestinations = [...new Set((legalMoves || [])
      .filter(move => typeof move === 'string' && move.slice(0, 2) === from)
      .map(move => move.slice(2, 4)))];
    return {action:'live', from, legalDestinations};
  }

  return {coordinateLabels, materialDifference, squareFromClientPoint, reconcilePartialPremove};
})();

if (typeof module !== 'undefined') module.exports = HebiChessUi;
