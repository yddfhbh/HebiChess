const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const js = fs.readFileSync('public/interaction-fixes.js', 'utf8');
const css = fs.readFileSync('public/interaction-fixes.css', 'utf8');
const html = fs.readFileSync('public/index.html', 'utf8');

test('live drag drop paints an immediate optimistic destination preview', () => {
  assert.match(js, /showOptimisticDrop/);
  assert.match(js, /pendingDropPreview/);
  assert.match(js, /selectedInteractionMode === 'live'/);
  assert.match(js, /promotionVariants\(drag\.from, destination\)\.length === 0/);
});

test('optimistic preview is cleared on server receive and move failure', () => {
  assert.match(js, /clearOptimisticDrop\(\);\s*if \(!data\.active/);
  assert.match(js, /catch\(error => \{\s*clearOptimisticDrop\(\)/);
});

test('optimistic preview does not intercept pointer input', () => {
  assert.match(css, /\.drop-preview[\s\S]*pointer-events:\s*none/);
});

test('drop latency assets use the next cache version', () => {
  assert.match(html, /interaction-fixes\.js\?v=20260909f/);
  assert.match(html, /interaction-fixes\.css\?v=20260909f/);
});
