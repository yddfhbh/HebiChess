import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const app = fs.readFileSync(new URL('../public/app.js', import.meta.url), 'utf8');
const context = { window: { addEventListener() {} }, console };
vm.runInNewContext(app, context);

const emptyBoard = () => Array.from({ length: 8 }, () => Array(8).fill(''));
const stateFor = (pieces, turn = 'white', castlingRights = { K: false, Q: false, k: false, q: false }) => {
  const board = emptyBoard();
  for (const [square, piece] of Object.entries(pieces)) {
    const file = square.charCodeAt(0) - 97;
    const row = 8 - Number(square[1]);
    board[row][file] = piece;
  }
  return {
    board,
    currentTurn: turn,
    castlingRights,
    enPassantTarget: null
  };
};
const move = (from, to, promotionPiece) => ({
  fromRow: 8 - Number(from[1]), fromCol: from.charCodeAt(0) - 97,
  toRow: 8 - Number(to[1]), toCol: to.charCodeAt(0) - 97, promotionPiece
});
const san = (pieces, from, to, promotionPiece, castlingRights) => context.buildSAN(stateFor(pieces, 'white', castlingRights), move(from, to, promotionPiece));

test('buildSAN covers ordinary, capture, castle, promotion, check, and mate SAN', () => {
  assert.equal(san({ e1: 'K', e2: 'P', e8: 'k' }, 'e2', 'e4'), 'e4');
  assert.equal(san({ e1: 'K', e4: 'P', d5: 'p', e8: 'k' }, 'e4', 'd5'), 'exd5');
  assert.equal(san({ e1: 'K', g1: 'N', e8: 'k' }, 'g1', 'f3'), 'Nf3');
  assert.equal(san({ e1: 'K', f3: 'N', e5: 'p', e8: 'k' }, 'f3', 'e5'), 'Nxe5');
  assert.equal(san({ e1: 'K', h1: 'R', h8: 'r', a8: 'k' }, 'h1', 'h8'), 'Rxh8+');
  assert.equal(san({ e1: 'K', h1: 'R', a8: 'k' }, 'e1', 'g1', null, { K: true, Q: false, k: false, q: false }), 'O-O');
  assert.equal(san({ e1: 'K', a1: 'R', h8: 'k' }, 'e1', 'c1', null, { K: false, Q: true, k: false, q: false }), 'O-O-O');
  assert.equal(san({ a1: 'K', e7: 'P', a8: 'k' }, 'e7', 'e8', 'Q'), 'e8=Q+');
  assert.equal(san({ a1: 'K', e7: 'P', d8: 'r', a8: 'k' }, 'e7', 'd8', 'Q'), 'exd8=Q+');
  assert.equal(san({ f6: 'K', e2: 'R', e8: 'k' }, 'e2', 'e7'), 'Re7+');
  assert.equal(san({ f7: 'K', g6: 'Q', h8: 'k' }, 'g6', 'h6'), 'Qh6#');
});

test('buildSAN uses legal-move disambiguation by file, rank, or both', () => {
  assert.equal(san({ e1: 'K', b1: 'N', f1: 'N', e8: 'k' }, 'b1', 'd2'), 'Nbd2');
  assert.equal(san({ a1: 'K', e1: 'R', e8: 'R', h7: 'k' }, 'e1', 'e2'), 'R1e2');
  assert.equal(san({ a1: 'K', b1: 'N', b3: 'N', f1: 'N', a8: 'k' }, 'b1', 'd2'), 'Nb1d2');
});

test('buildPGN emits portable JJUGLE PGN without clock annotations', () => {
  context.gameSetting = { ...context.gameSetting, mode: 'ai', whiteName: '플레이어', blackName: 'JJUGLE', unlimited: false, minutes: 3, increment: 0 };
  context.gameStartedAt = new Date('2026-09-22T00:00:00Z');
  context.finalGameResult = '0-1';
  context.moveHistory = [
    { color: 'white', notation: 'e4', durationMs: 1000, clockAfterMove: 179 },
    { color: 'black', notation: 'c5', durationMs: 900, clockAfterMove: 180 }
  ];
  const pgn = context.buildPGN();
  for (const tag of ['Event', 'Site', 'Date', 'Round', 'White', 'Black', 'Result']) {
    assert.match(pgn, new RegExp(`^\\[${tag} "`, 'm'));
  }
  assert.match(pgn, /^\[Event "JJUGLE Game"\]$/m);
  assert.match(pgn, /1\. e4 c5 0-1/);
  assert.doesNotMatch(pgn, /\[%timestamp|\[%clk/);
  assert.doesNotMatch(pgn, /Termination|EndTime|Local Chess/);
});
