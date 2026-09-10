const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const js = fs.readFileSync('public/interaction-fixes.js', 'utf8');
const css = fs.readFileSync('public/interaction-fixes.css', 'utf8');
const html = fs.readFileSync('public/index.html', 'utf8');

test('interaction hardening assets are loaded after the main client', () => {
  assert.match(html, /app\.js\?v=20260911r1[\s\S]*interaction-fixes\.js\?v=20260909f/);
  assert.match(html, /interaction-fixes\.css\?v=20260909f/);
});

test('partial premove reconciliation refreshes live selection before rendering', () => {
  assert.match(js, /reconcilePartialSelection/);
  assert.match(js, /selectedInteractionMode = 'live'/);
  assert.match(js, /legalDestinations = decision\.legalDestinations/);
  assert.match(js, /maybePremove\(\)/);
});

test('drag drop uses board geometry instead of elementFromPoint', () => {
  assert.match(js, /squareFromClientPoint/);
  assert.match(js, /getBoundingClientRect\(\)/);
  assert.doesNotMatch(js, /elementFromPoint/);
});

test('drag ghost is fixed, non-interactive and high z-index', () => {
  assert.match(css, /position:\s*fixed/);
  assert.match(css, /pointer-events:\s*none/);
  assert.match(css, /z-index:\s*200/);
});

test('drag click suppression auto clears instead of consuming a later real click', () => {
  assert.match(js, /suppressClick = true/);
  assert.match(js, /setTimeout\(\(\) => \{ suppressClick = false; \}, 0\)/);
});
