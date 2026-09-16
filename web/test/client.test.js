const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const app = fs.readFileSync('public/app.js', 'utf8');
const html = fs.readFileSync('public/index.html', 'utf8');
const css = fs.readFileSync('public/style.css', 'utf8');

test('board client exposes thresholded pointer drag, edge coordinates, SVG paths and flip', () => {
  assert.match(app, /pointerdown/);
  assert.match(app, /pointermove/);
  assert.match(app, /pointerup/);
  assert.match(app, /function pieceSvg/);
  assert.match(app, /function boardPosition/);
  assert.match(app, /flipped=!flipped/);
  assert.match(html, /id="flip"/);
  assert.match(app, /setPointerCapture/);
  assert.match(app, /document\.elementFromPoint/);
  assert.match(app, /distance>=6/);
  assert.match(app, /pointercancel/);
  assert.match(app, /PIECE_SVG/);
  assert.match(css, /edge-coordinates/);
});

test('premove, history navigation and spectator controls are client guarded', () => {
  assert.match(app, /premove=/);
  assert.match(app, /function maybePremove/);
  assert.match(app, /S\.turn!==S\.playerColor/);
  assert.match(app, /function setView/);
  assert.match(app, /function latest/);
  assert.match(app, /function isPlayer/);
  assert.match(app, /if\(!isPlayer\(\)\|\|!live\(\)\|\|S\.result\)return/);
  assert.match(html, /id="cancel-premove"/);
});

test('game-over modal and resign confirmation are present', () => {
  assert.match(html, /id="modal" class="modal" hidden/);
  assert.match(html, /id="promotion-modal" class="modal" hidden/);
  assert.match(css, /\.modal\[hidden\]\s*\{[^}]*display\s*:\s*none/);
  assert.match(app, /function showGameOver/);
  assert.match(app, /정말 기권하시겠습니까/);
});

test('premove previews double pawn and castling, queues promotion, and uses a modal', () => {
  assert.match(app, /row\+d\*2/);
  assert.match(app, /out\.push\(side==='w'\?'g1':'g8'/);
  assert.match(app, /function showPromotion/);
  assert.match(app, /requestInFlight/);
  assert.doesNotMatch(app, /prompt\(/);
});

test('history plies are individually clickable and historical check comes from snapshot', () => {
  assert.match(app, /cell\.onclick=\(\)=>setView\(ply\+1\)/);
  assert.match(app, /position\.checkSquare/);
  assert.match(html, /id="first"/);
  assert.match(html, /id="latest-small"/);
});


test('all player moves fail closed through a browser snapshot', () => {
  assert.match(app, /const submitPlayerMove=gameMutations\.move\.bind\(gameMutations\)/);
  assert.match(app, /gameMutations\.move=async \(move,snapshot\)=>submitPlayerMove\(move,snapshot\|\|await browserSnapshot\(move\)\)/);
});

test('NNUE selection is explicit and fails closed without changing the HCE default', () => {
  assert.match(app, /get\('eval'\)\?\.toUpperCase\(\)==='NNUE'\?'NNUE':'HCE'/);
  assert.match(app, /await requestedNnueLoad;await client\.setEvalMode\('NNUE'\)/);
  assert.match(app, /NNUE를 사용할 수 없습니다/);
});
