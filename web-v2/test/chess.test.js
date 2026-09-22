import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

const app = fs.readFileSync(new URL('../public/app.js', import.meta.url), 'utf8');
const html = fs.readFileSync(new URL('../public/index.html', import.meta.url), 'utf8');

test('imports the original chess UI and rule interaction surface', () => {
  for (const name of [
    'PIECE_SVG', 'castlingRights', 'enPassantTarget', 'capturedByWhite',
    'capturedByBlack', 'moveHistory', 'boardHistory', 'renderBoard',
    'executeMove', 'legalMoves', 'showPromotionModal'
  ]) assert.match(app, new RegExp(`\\b${name}\\b`));
  assert.match(app, /var premoveQueue\s*=\s*\[\]/);
  for (const name of ['buildPremovePreviewState', 'relaxedPremoveMovesForState', 'queuePremove', 'tryExecutePremove']) {
    assert.match(app, new RegExp(`function ${name}\\s*\\(`));
  }
  assert.match(app, /pointerdown|touchstart/);
  assert.match(app, /dragGhost/);
  assert.match(html, /id="chessboard"/);
  assert.match(html, /id="promotion-modal"/);
});

test('does not expose online or Firebase UI', () => {
  assert.doesNotMatch(html, /firebase|matchmaking|방 만들기|방 참가|온라인 PvP/i);
  assert.doesNotMatch(app, /firebase|matchmaking|setoption name Skill Level|go depth/i);
});
