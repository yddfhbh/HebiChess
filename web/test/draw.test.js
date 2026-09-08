const test = require('node:test');
const assert = require('node:assert/strict');
const chess = require('../server');

function cleanGame(board, turn = 'w', castles = '-', ep = '-', halfmove = 0) {
  const key = chess.positionKey(board, turn, castles, ep);
  return {active:true, playerSessionId:'draw-test', playerColor:'w', board, castles, ep, halfmove, moves:[], san:[], turn, engineThinking:false, result:null, termination:null, lastMove:null, depth:0, evaluation:0, startedAt:new Date().toISOString(), repetitions:{[key]:1}, positionHistory:[key], history:[{uci:null,san:null,board:board.map(row=>row.slice()),turn,lastMove:null}]};
}
function board(pieces) {
  const value = Array.from({length:8}, () => Array(8).fill('.'));
  for (const [square, piece] of Object.entries(pieces)) value[8-Number(square[1])][square.charCodeAt(0)-97] = piece;
  return value;
}

test('threefold repetition includes side, castling rights and en passant', () => {
  chess.start('draw-threefold', 'white');
  for (const value of ['g1f3','g8f6','f3g1','f6g8','g1f3','g8f6','f3g1','f6g8']) chess.apply(chess.legal(value));
  assert.equal(chess.drawReason(), 'threefold repetition');
  assert.notEqual(chess.positionKey(chess.boardStart(), 'w', 'KQkq', '-'), chess.positionKey(chess.boardStart(), 'w', '-', '-'));
  assert.notEqual(chess.positionKey(chess.boardStart(), 'w', 'KQkq', 'e3'), chess.positionKey(chess.boardStart(), 'w', 'KQkq', '-'));
  chess.setGame(null);
});

test('halfmove reaches automatic 50-move draw after a quiet move', () => {
  chess.start('draw-fifty', 'white');
  chess.getGame().halfmove = 99;
  chess.apply(chess.legal('g1f3'));
  assert.equal(chess.getGame().halfmove, 100);
  assert.equal(chess.drawReason(), '50-move rule');
  chess.setGame(null);
});

test('insufficient material recognizes king-only, bishop and knight endings', () => {
  assert.equal(chess.insufficientMaterial(board({e1:'K',e8:'k'})), true);
  assert.equal(chess.insufficientMaterial(board({e1:'K',c1:'B',e8:'k'})), true);
  assert.equal(chess.insufficientMaterial(board({e1:'K',c1:'N',e8:'k'})), true);
  assert.equal(chess.insufficientMaterial(board({e1:'K',c1:'B',e8:'k',f8:'r'})), false);
});
