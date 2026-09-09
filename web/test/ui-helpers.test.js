const test = require('node:test');
const assert = require('node:assert/strict');
const {coordinateLabels, materialDifference} = require('../public/ui-helpers');
const chess = require('../server');
const {whitePovEvaluation} = chess;

test('coordinates follow the board orientation for both perspectives', () => {
  assert.deepEqual(coordinateLabels(false), {files:['a','b','c','d','e','f','g','h'], ranks:['8','7','6','5','4','3','2','1']});
  assert.deepEqual(coordinateLabels(true), {files:['h','g','f','e','d','c','b','a'], ranks:['1','2','3','4','5','6','7','8']});
});

test('material difference uses pieces captured by each side', () => {
  assert.equal(materialDifference({w:['r'], b:['b','p']}), 1);
  assert.equal(materialDifference({w:['n'], b:['r']}), -2);
  assert.equal(materialDifference({w:['r'], b:['r']}), 0);
});

test('capturedPieces reports the side that made each capture', () => {
  chess.start('captured-pieces-test', 'white');
  for (const move of ['e2e4', 'd7d5', 'e4d5']) chess.apply(chess.legal(move));
  assert.deepEqual(chess.currentState('captured-pieces-test').capturedPieces, {w:['p'], b:[]});
  chess.setGame(null);
});

test('engine centipawns are normalized from root side to White POV', () => {
  assert.equal(whitePovEvaluation(227, 'w'), 2.27);
  assert.equal(whitePovEvaluation(227, 'b'), -2.27);
  assert.equal(whitePovEvaluation(-80, 'b'), 0.8);
});
