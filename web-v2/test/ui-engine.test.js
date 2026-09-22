import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { newGame } from '../public/chess.js';
import { ENGINE_MOVETIME_MS, parseBestmove, queuePremove, tryExecutePremove } from '../public/app.js';

const appSource = fs.readFileSync(new URL('../public/app.js', import.meta.url), 'utf8');
const workerSource = fs.readFileSync(new URL('../public/engine/jjugle-worker.js', import.meta.url), 'utf8');

test('uses JJUGLE UCI adapter and parses bestmove', () => {
  assert.equal(ENGINE_MOVETIME_MS, 1500);
  assert.equal(parseBestmove('bestmove e2e4 ponder e7e5'), 'e2e4');
  assert.match(appSource, /new Worker\('\.\/engine\/jjugle-worker\.js'\)/);
  assert.doesNotMatch(appSource, /Stockfish|Firebase|matchmaking/i);
  assert.match(appSource, /position fen/); // UI speaks the small UCI command surface
  assert.match(workerSource, /go \|setoption/); // adapter forwards UCI commands to WASM
});

test('queues and executes a premove only on the player turn', () => {
  const state = newGame();
  const queue = queuePremove([], { color: 'w', uci: 'e2e4' });
  const waiting = tryExecutePremove({ ...state, turn: 'b' }, queue);
  assert.equal(waiting.queue.length, 1);
  const executed = tryExecutePremove(state, queue);
  assert.equal(executed.queue.length, 0);
  assert.equal(executed.state.board[36], 'P');
});

test('engine telemetry does not call board rendering', () => {
  const infoStart = appSource.indexOf('function renderTelemetry');
  const infoEnd = appSource.indexOf('function renderBoard');
  assert.ok(infoStart >= 0 && infoEnd > infoStart);
  assert.doesNotMatch(appSource.slice(infoStart, infoEnd), /renderBoard\(/);
});
