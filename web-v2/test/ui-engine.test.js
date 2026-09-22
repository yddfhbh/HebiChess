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

test('guards JJUGLE moves and avoids duplicate worker output', () => {
  assert.match(workerSource, /function command\(text\)\s*\{\s*engine\.ccall\('hebichess_send_command'/);
  assert.doesNotMatch(workerSource, /function command\(text\)[\s\S]*?emitBuffer\(\)/);
  assert.match(appSource, /if \(gameOver \|\| !aiThinking \|\| !isAITurn\(\)\)/);
  assert.match(appSource, /pieceColor\(piece\) !== gameSetting\.aiColor/);
  assert.match(appSource, /isLegalDestination/);
  assert.match(appSource, /aiThinking = false;\s*showAIThinking\(false\);\s*executeMove/);
});

test('uses the game time modal for AI setup', () => {
  const html = fs.readFileSync(new URL('../public/index.html', import.meta.url), 'utf8');
  assert.match(html, /onclick="openGameTimeModal\('ai'\)"/);
});
