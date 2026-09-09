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

  return {coordinateLabels, materialDifference};
})();

if (typeof module !== 'undefined') module.exports = HebiChessUi;
