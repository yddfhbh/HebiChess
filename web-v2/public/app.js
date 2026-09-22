import { newGame, legalMoves, play, square, toFen, status, boardFromFen, colorOf, pieceType } from './chess.js';

export const ENGINE_MOVETIME_MS = 1500;
export const MODEL_METADATA = { file: 'models/hebinnue-v3-4c815d54bc6c9fbf.hebinnue', sha256: '4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826' };
export const BOOK_METADATA = { file: 'books/witty_alien-v1-0edeb48aeb553d16.hebibook', sha256: '0edeb48aeb553d16fb79af5de01e43f422ee8c162b14f73b40b27972cb4c50a9', bytes: 1889392 };

export function parseBestmove(line) { const match = String(line).match(/^bestmove\s+([a-h][1-8][a-h][1-8][qrbn]?)/); return match?.[1] || null; }
export function fenFromState(state) { return toFen(state); }
export function queuePremove(queue, move) { return [...queue, move]; }
export function tryExecutePremove(state, queue) { const move = queue[0]; if (!move || move.color !== state.turn) return { state, queue }; const next = play(state, move.uci); return next ? { state: next, queue: queue.slice(1) } : { state, queue: [] }; }

const GLYPHS = { k: '♚', q: '♛', r: '♜', b: '♝', n: '♞', p: '♟' };
const app = { state: null, color: null, selected: null, premoveQueue: [], flipped: false, worker: null, engineReady: null, engineSearch: false, pendingPromotion: null };
const $ = id => document.getElementById(id);
const playerTurn = () => app.state?.turn === app.color;

function setStatus(text, kind = '') { const node = $('status'); node.textContent = text; node.className = kind; }
function boardIndex(row, col) { return (app.flipped ? 7 - row : row) * 8 + (app.flipped ? 7 - col : col); }
function renderTelemetry(info) {
  if (info.depth !== undefined) $('depth').textContent = info.depth ?? '—';
  if (info.nodes !== undefined) $('nodes').textContent = info.nodes ?? '—';
  if (info.score !== undefined) $('evaluation').textContent = info.score;
}
function renderBoard() {
  const board = $('board'); board.replaceChildren();
  const legal = app.selected === null ? [] : legalMoves(app.state, app.selected).map(move => move.to);
  for (let row = 0; row < 8; row++) for (let col = 0; col < 8; col++) {
    const index = boardIndex(row, col), cell = document.createElement('button'); cell.className = `square ${(row + col) % 2 ? 'dark' : 'light'}`; cell.dataset.index = index;
    if (app.selected === index) cell.classList.add('selected'); if (legal.includes(index)) cell.classList.add('legal'); if (app.state.lastMove && [app.state.lastMove.from, app.state.lastMove.to].includes(index)) cell.classList.add('last-move');
    const piece = app.state.board[index]; if (piece !== '.') { cell.textContent = GLYPHS[pieceType(piece)]; cell.classList.add(colorOf(piece) === 'w' ? 'white-piece' : 'black-piece'); cell.draggable = colorOf(piece) === app.color; }
    cell.setAttribute('aria-label', `${square(index)}${piece === '.' ? '' : ` ${piece}`}`); cell.addEventListener('click', () => selectSquare(index)); cell.addEventListener('dragstart', event => { event.dataTransfer.setData('text/plain', String(index)); app.selected = index; }); cell.addEventListener('dragover', event => event.preventDefault()); cell.addEventListener('drop', event => { event.preventDefault(); moveTo(index, Number(event.dataTransfer.getData('text/plain'))); }); board.append(cell);
  }
  $('turn').textContent = app.state.turn === app.color ? 'Your turn' : 'JJUGLE is thinking';
  $('moves').textContent = app.state.san.map((move, i) => `${i % 2 ? `${Math.floor(i / 2) + 1}...` : `${Math.floor(i / 2) + 1}.`} ${move}`).join('  ');
  $('captured').textContent = app.state.captured.map(piece => GLYPHS[piece]).join(' ');
}
function selectSquare(index) {
  if (!app.state || app.state.result) return;
  const piece = app.state.board[index];
  if (app.selected !== null) { if (moveTo(index, app.selected)) return; }
  if (piece !== '.' && colorOf(piece) === app.color) { app.selected = index; renderBoard(); }
  else if (!playerTurn()) { const candidate = { from: app.selected ?? index, to: index, color: app.color, uci: `${square(app.selected ?? index)}${square(index)}` }; app.premoveQueue = queuePremove(app.premoveQueue, candidate); setStatus('Premove queued', 'notice'); }
}
function moveTo(to, from = app.selected) {
  if (from === null || from === undefined) return false;
  const candidates = legalMoves(app.state, from).filter(move => move.to === to);
  if (!candidates.length) return false;
  if (candidates.some(move => move.promotion)) { app.pendingPromotion = { from, to }; $('promotion').hidden = false; return true; }
  return commit(`${square(from)}${square(to)}`);
}
function commit(uci) {
  const next = play(app.state, uci); if (!next) return false;
  app.state = next; app.selected = null; renderBoard(); finishOrContinue(); return true;
}
function finishOrContinue() {
  const outcome = status(app.state); app.state.result = outcome.result; if (outcome.result) { setStatus(outcome.status === 'checkmate' ? `Checkmate — ${outcome.result}` : 'Draw — stalemate'); renderBoard(); return; }
  setStatus(outcome.status === 'check' ? 'Check' : '');
  if (app.state.turn !== app.color) requestAIMove(); else { const before = app.state; const result = tryExecutePremove(app.state, app.premoveQueue); app.state = result.state; app.premoveQueue = result.queue; renderBoard(); if (result.state !== before) finishOrContinue(); }
}
function connectWorker() {
  app.worker = new Worker('./engine/jjugle-worker.js');
  app.engineReady = new Promise((resolve, reject) => { const onMessage = event => { if (typeof event.data === 'string') { const line = event.data; if (line === 'uciok') resolve(); const move = parseBestmove(line); if (move && app.engineSearch) { app.engineSearch = false; commit(move); } const info = line.match(/^info .*?(?:depth (\d+))?.*?(?:score (cp|mate) (-?\d+))?.*?(?:nodes (\d+))?/); if (info) renderTelemetry({ depth: info[1] ? Number(info[1]) : undefined, nodes: info[4] ? Number(info[4]) : undefined, score: info[2] ? `${info[2] === 'cp' ? (Number(info[3]) / 100).toFixed(2) : `M${info[3]}`}` : undefined }); } else if (event.data?.type === 'error') reject(new Error(event.data.message)); }; app.worker.addEventListener('message', onMessage); });
  app.worker.postMessage('uci'); return app.engineReady;
}
async function requestAIMove() {
  if (app.engineSearch || app.state.result) return; app.engineSearch = true; setStatus('JJUGLE is thinking', 'thinking');
  try { await app.engineReady; app.worker.postMessage(`position fen ${toFen(app.state)}`); app.worker.postMessage(`go movetime ${ENGINE_MOVETIME_MS}`); } catch (error) { app.engineSearch = false; setStatus(`JJUGLE error: ${error.message}`, 'error'); }
}
function start(color) {
  $('lobby').hidden = true; $('game').hidden = false; app.color = color === 'random' ? (Math.random() < 0.5 ? 'w' : 'b') : color; app.state = newGame(); app.flipped = app.color === 'b'; app.premoveQueue = []; app.selected = null; $('player-color').textContent = app.color === 'w' ? 'White' : 'Black'; renderBoard(); setStatus('JJUGLE is loading', 'thinking');
  connectWorker().then(() => { setStatus(app.color === 'w' ? 'Your turn' : 'JJUGLE is thinking'); if (app.color === 'b') requestAIMove(); }).catch(error => setStatus(`JJUGLE error: ${error.message}`, 'error'));
}
if (typeof document !== 'undefined') {
  document.querySelectorAll('[data-color]').forEach(button => button.addEventListener('click', () => start(button.dataset.color)));
  $('flip').addEventListener('click', () => { app.flipped = !app.flipped; renderBoard(); });
  document.querySelectorAll('[data-promotion]').forEach(button => button.addEventListener('click', () => { const move = app.pendingPromotion; app.pendingPromotion = null; $('promotion').hidden = true; if (move) commit(`${square(move.from)}${square(move.to)}${button.dataset.promotion}`); }));
}
if (typeof window !== 'undefined') window.JJUGLE = { app, start, renderBoard, requestAIMove };
