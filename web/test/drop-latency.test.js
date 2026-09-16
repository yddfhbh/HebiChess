const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const js = fs.readFileSync('public/drop-latency.js', 'utf8');
const css = fs.readFileSync('public/drop-latency.css', 'utf8');
const html = fs.readFileSync('public/index.html', 'utf8');

test('live drag drop paints an immediate optimistic destination preview', () => {
  assert.match(js, /showOptimisticDrop/);
  assert.match(js, /pendingDropPreview/);
  assert.match(js, /selectedInteractionMode === 'live'/);
  assert.match(js, /promotionVariants\(drag\.from, destination\)\.length === 0/);
});


test('optimistic capture hides the captured piece immediately', () => {
  assert.match(js, /destinationPiece/);
  assert.match(js, /destinationPiece\.style\.visibility = 'hidden'/);
  assert.match(js, /destinationPiece\?\.style\.removeProperty\('visibility'\)/);
});

test('committed move clears stale move markers immediately', () => {
  assert.match(js, /function clearImmediateMoveMarkers/);
  assert.match(js, /clearImmediateMoveMarkers\(\)/);
  assert.match(js, /\.selected,.legal,.capture/);
});

test('click moves also receive immediate optimistic feedback', () => {
  assert.match(js, /const from = move\.slice\(0, 2\)/);
  assert.match(js, /const to = move\.slice\(2, 4\)/);
  assert.match(js, /pieceAt\(from\)/);
});

test('optimistic preview is cleared on server receive and move failure', () => {
  assert.match(js, /receive\s*=\s*function receiveWithDropPreviewCleanup\(data, eventType\) \{\s*clearOptimisticDrop\(\);\s*return previousReceive\(data, eventType\);/);
  assert.match(js, /canonicalSendMove\(move\)/);
});

test('optimistic drop uses the shared revisioned move path', () => {
  assert.match(js, /const canonicalSendMove = sendMove/);
  assert.match(js, /canonicalSendMove\(move\)/);
  assert.doesNotMatch(js, /api\('\/api\/move',\{move\}\)/);
});

test('optimistic drop cannot bypass state apply and engine reconciliation', () => {
  assert.doesNotMatch(js, /S=data/);
});

test('optimistic preview does not intercept pointer input', () => {
  assert.match(css, /\.drop-preview[\s\S]*pointer-events:\s*none/);
});

test('drop latency assets use the next cache version', () => {
  assert.match(html, /drop-latency\.js\?v=20260915a/);
  assert.match(html, /drop-latency\.css\?v=20260909f/);
});
