const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const js = fs.readFileSync('public/interaction-fixes.js', 'utf8');
const html = fs.readFileSync('public/index.html', 'utf8');

test('remote state preserves an active drag when the source survives', () => {
  assert.match(js, /function preserveActiveDragAcrossRender/);
  assert.match(js, /const activeDrag = drag && drag\.dragging/);
  assert.match(js, /drag\.source = document\.querySelector/);
  assert.match(js, /applyDragMoveHints\(\)/);
});

test('active premove drag is converted to fresh live targets when the turn returns', () => {
  assert.match(js, /selectedInteractionMode = 'live'/);
  assert.match(js, /legalDestinations = decision\.legalDestinations/);
  assert.match(js, /drag\.piece = pieceAt\(decision\.from\)/);
});

test('drag source disappearance still cancels the interaction', () => {
  assert.match(js, /if \(decision\.action === 'clear'\)/);
  assert.match(js, /clearInteraction\(\)/);
});

test('drag pointer position is retained across board rerenders', () => {
  assert.match(js, /drag\.lastX = event\.clientX/);
  assert.match(js, /drag\.lastY = event\.clientY/);
  assert.match(js, /moveGhost\(\{clientX: drag\.lastX, clientY: drag\.lastY\}\)/);
});

test('interaction assets use the continuity cache version', () => {
  assert.match(html, /interaction-fixes\.js\?v=20260909e/);
  assert.match(html, /interaction-fixes\.css\?v=20260909e/);
});
