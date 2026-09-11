const test = require('node:test');
const assert = require('node:assert/strict');
const {coordinateLabels, materialDifference, squareFromClientPoint, reconcilePartialPremove} = require('../public/ui-helpers');
const {whitePovEvaluation} = require('../server');

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
  const browserSnapshot = {capturedPieces:{w:['p'], b:[]}, moves:['e2e4','d7d5','e4d5']};
  assert.deepEqual(browserSnapshot.capturedPieces, {w:['p'], b:[]});
  assert.equal(browserSnapshot.moves.at(-1), 'e4d5');
});

test('engine centipawns are normalized from root side to White POV', () => {
  assert.equal(whitePovEvaluation(227, 'w'), 2.27);
  assert.equal(whitePovEvaluation(227, 'b'), -2.27);
  assert.equal(whitePovEvaluation(-80, 'b'), 0.8);
});

test('client point maps to chess squares in both orientations', () => {
  const rect = {left:100, top:200, width:800, height:800};
  assert.equal(squareFromClientPoint(101, 201, rect, false), 'a8');
  assert.equal(squareFromClientPoint(899, 999, rect, false), 'h1');
  assert.equal(squareFromClientPoint(101, 201, rect, true), 'h1');
  assert.equal(squareFromClientPoint(899, 999, rect, true), 'a8');
  assert.equal(squareFromClientPoint(450, 550, rect, false), 'd5');
  assert.equal(squareFromClientPoint(450, 550, rect, true), 'e4');
  assert.equal(squareFromClientPoint(99, 201, rect, false), null);
  assert.equal(squareFromClientPoint(900, 201, rect, false), null);
});

test('partial premove becomes a live selection with fresh legal targets', () => {
  const decision = reconcilePartialPremove({
    mode:'premove',
    from:'f3',
    hasQueuedPremove:false,
    playerTurn:true,
    sourceIsOwnPiece:true,
    legalMoves:['f3e5','f3g5','a2a3','f3h4']
  });
  assert.deepEqual(decision, {action:'live', from:'f3', legalDestinations:['e5','g5','h4']});
});

test('partial premove is cleared if the selected piece disappeared', () => {
  assert.deepEqual(reconcilePartialPremove({
    mode:'premove', from:'f3', hasQueuedPremove:false, playerTurn:true, sourceIsOwnPiece:false, legalMoves:[]
  }), {action:'clear'});
});

test('completed premove is left alone for automatic execution', () => {
  assert.deepEqual(reconcilePartialPremove({
    mode:'premove', from:'f3', hasQueuedPremove:true, playerTurn:true, sourceIsOwnPiece:true, legalMoves:['f3e5']
  }), {action:'keep'});
});
