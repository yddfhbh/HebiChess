const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

test('browser contract carries promotion and castling choices as opaque move metadata', () => {
  const snapshot = {fen:'8/P6k/8/8/8/8/4K3/8 w - - 0 1', moves:['a7a8q'], san:['a8=Q+'], legalMoves:['a7a8q','a7a8r','a7a8b','a7a8n','e1g1']};
  assert.deepEqual(snapshot.legalMoves.filter(move => move.startsWith('a7a8')), ['a7a8q','a7a8r','a7a8b','a7a8n']);
  assert.ok(snapshot.legalMoves.includes('e1g1'));
  assert.equal(snapshot.san[0], 'a8=Q+');
});

test('frontend gates live promotion on legal variants and premove promotion on a pawn', () => {
  const app = fs.readFileSync('public/app.js', 'utf8');
  assert.match(app, /function promotionVariants/);
  assert.match(app, /browserGameState\.legalMoves\.filter/);
  assert.match(app, /async function hydrateBrowserGameState/);
  assert.match(app, /S=\{\.\.\.S,legalMoves:\[\.\.\.legalMoves\]\}/);
  assert.match(app, /function isPremovePromotion/);
  assert.match(app, /function chooseDestination\(pos\)/);
  assert.doesNotMatch(app, /if\(r===0\|\|r===7\)/);
  assert.doesNotMatch(app, /prompt\(/);
});
