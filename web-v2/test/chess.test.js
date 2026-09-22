import test from 'node:test';
import assert from 'node:assert/strict';
import { newGame, legalMoves, play, fromFen, status, toFen } from '../public/chess.js';

test('generates legal opening moves and FEN after a move', () => {
  const state = newGame();
  assert.equal(legalMoves(state).length, 20);
  const next = play(state, 'e2e4');
  assert.equal(toFen(next), 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1');
});

test('supports castling, en passant, and promotion', () => {
  const castle = fromFen('r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1');
  assert.ok(legalMoves(castle).some(move => move.from === 60 && move.to === 62));
  assert.equal(play(castle, 'e1g1').board[61], 'R');
  const enPassant = fromFen('8/8/8/3pP3/8/8/8/4K2k w - d6 0 1');
  assert.equal(play(enPassant, 'e5d6').board[19], 'P');
  const promotion = fromFen('4k3/P7/8/8/8/8/8/4K3 w - - 0 1');
  assert.equal(play(promotion, 'a7a8q').board[0], 'Q');
});

test('detects checkmate and stalemate', () => {
  assert.deepEqual(status(fromFen('7k/6Q1/6K1/8/8/8/8/8 b - - 0 1')), { status: 'checkmate', result: '1-0' });
  assert.deepEqual(status(fromFen('7k/5Q2/7K/8/8/8/8/8 b - - 0 1')), { status: 'stalemate', result: '1/2-1/2' });
});
