import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

const appSource = fs.readFileSync(new URL('../public/app.js', import.meta.url), 'utf8');
const workerSource = fs.readFileSync(new URL('../public/engine/jjugle-worker.js', import.meta.url), 'utf8');
const timerSource = fs.readFileSync(new URL('../public/first-move-timer.js', import.meta.url), 'utf8');

test('uses the JJUGLE UCI adapter without Stockfish settings', () => {
  assert.match(appSource, /new Worker\('\.\/engine\/jjugle-worker\.js'\)/);
  assert.match(appSource, /position fen/);
  assert.match(appSource, /go movetime 1500/);
  assert.doesNotMatch(appSource, /Stockfish|setoption name Skill Level|go depth/i);
  assert.match(workerSource, /go \|setoption/);
});

test('retains premove, drag, promotion, and timer support', () => {
  for (const name of ['buildPremovePreviewState', 'relaxedPremoveMovesForState', 'queuePremove', 'tryExecutePremove', 'cleanupDragState', 'executeMove']) {
    assert.match(appSource, new RegExp(`function ${name}\\s*\\(`));
  }
  assert.match(timerSource, /FIRST_MOVE_TIMEOUT_MS/);
});
