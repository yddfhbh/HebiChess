const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

const app = fs.readFileSync('public/app.js', 'utf8');
const html = fs.readFileSync('public/index.html', 'utf8');
const css = fs.readFileSync('public/style.css', 'utf8');

test('board client exposes click, pointer drag, coordinates, SVG pieces and flip', () => {
  assert.match(app, /pointerdown/);
  assert.match(app, /pointermove/);
  assert.match(app, /pointerup/);
  assert.match(app, /function pieceSvg/);
  assert.match(app, /function boardPosition/);
  assert.match(app, /flipped=!flipped/);
  assert.match(html, /id="flip"/);
  assert.match(css, /\.coord/);
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
  assert.match(html, /id="modal"/);
  assert.match(app, /function showGameOver/);
  assert.match(app, /정말 기권하시겠습니까/);
});
