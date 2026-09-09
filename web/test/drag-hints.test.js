const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const js = fs.readFileSync('public/interaction-fixes.js', 'utf8');
const css = fs.readFileSync('public/interaction-fixes.css', 'utf8');

test('drag activation paints every legal destination until drag cleanup', () => {
  assert.match(js, /function applyDragMoveHints\(\)/);
  assert.match(js, /legalDestinations\.forEach/);
  assert.match(js, /drag-legal/);
  assert.match(js, /drag-capture/);
  assert.match(js, /applyDragMoveHints\(\);/);
  assert.match(js, /function clearDragMoveHints\(\)/);
  assert.match(js, /clearDragMoveHints\(\);/);
});

test('drag legal and capture hints reuse visible board move markers', () => {
  assert.match(css, /\.square\.drag-legal:after/);
  assert.match(css, /\.square\.drag-capture:after/);
});
