const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {pseudo} = require('../server');

const empty = () => Array.from({length:8}, () => Array(8).fill('.'));
const put = (board, pieces) => { for (const [position, piece] of Object.entries(pieces)) {
  const file = position.charCodeAt(0) - 97, row = 8 - Number(position[1]);
  board[row][file] = piece;
} return board; };
const moves = (board, side) => pseudo(board, side, '', '-').map(m => `${m.from}${m.to}${m.promotion || ''}`);

test('promotion move metadata exists only for pawn promotions', () => {
  const white = moves(put(empty(), {e1:'K', a8:'k', e7:'P', f8:'r'}), 'w');
  assert.deepEqual(white.filter(m => m.startsWith('e7e8')), ['e7e8q','e7e8r','e7e8b','e7e8n']);
  assert.deepEqual(white.filter(m => m.startsWith('e7f8')), ['e7f8q','e7f8r','e7f8b','e7f8n']);
  const black = moves(put(empty(), {e8:'k', a1:'K', e2:'p', f1:'R'}), 'b');
  assert.deepEqual(black.filter(m => m.startsWith('e2e1')), ['e2e1q','e2e1r','e2e1b','e2e1n']);
  assert.deepEqual(black.filter(m => m.startsWith('e2f1')), ['e2f1q','e2f1r','e2f1b','e2f1n']);
});

test('castling destinations are ordinary king moves, never promotion variants', () => {
  const board = put(empty(), {e1:'K', a1:'R', h1:'R', e8:'k'});
  const legal = pseudo(board, 'w', 'KQ', '-');
  assert.ok(legal.some(m => m.from === 'e1' && m.to === 'g1' && !m.promotion));
  assert.ok(legal.some(m => m.from === 'e1' && m.to === 'c1' && !m.promotion));
  assert.ok(!legal.some(m => m.from === 'e1' && m.to === 'g1' && m.promotion));
  assert.ok(!legal.some(m => m.from === 'e1' && m.to === 'c1' && m.promotion));
  const blackBoard = put(empty(), {e8:'k', a8:'r', h8:'r', e1:'K'});
  const blackLegal = pseudo(blackBoard, 'b', 'kq', '-');
  assert.ok(blackLegal.some(m => m.from === 'e8' && m.to === 'g8' && !m.promotion));
  assert.ok(blackLegal.some(m => m.from === 'e8' && m.to === 'c8' && !m.promotion));
  assert.ok(!blackLegal.some(m => m.from === 'e8' && m.to === 'g8' && m.promotion));
  assert.ok(!blackLegal.some(m => m.from === 'e8' && m.to === 'c8' && m.promotion));
});

test('frontend gates live promotion on legal variants and premove promotion on a pawn', () => {
  const app = fs.readFileSync('public/app.js', 'utf8');
  assert.match(app, /function promotionVariants/);
  assert.match(app, /\(S\.legalMoves\|\|\[\]\)\.filter/);
  assert.match(app, /function isPremovePromotion/);
  assert.match(app, /function chooseDestination\(pos\)/);
  assert.doesNotMatch(app, /if\(r===0\|\|r===7\)/);
  assert.doesNotMatch(app, /prompt\(/);
});
