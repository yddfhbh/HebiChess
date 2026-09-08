const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');

const root = __dirname;
const PUBLIC_ROOT = path.join(root, 'public') + path.sep;
const PORT = Number(process.env.PORT || 3400);
const ENGINE = path.resolve(root, process.env.HEBICHESS_BINARY || '../build/HebiChess');
const DEPTH = Number(process.env.DEFAULT_SEARCH_DEPTH || 7);
const DATA = path.resolve(root, process.env.DATA_PATH || './data/games.json');
const PRODUCTION = process.env.NODE_ENV === 'production';
const clients = new Map();
let game = null, engine = null, serial = Promise.resolve();

const files = {'.html':'text/html; charset=utf-8','.css':'text/css','.js':'text/javascript'};
const copy = board => board.map(row => row.slice());
const id = () => crypto.randomUUID();
function cookie(req, res) {
  const match = (req.headers.cookie || '').match(/hebichess_session=([^;]+)/);
  if (match) return match[1];
  const value = id();
  res.setHeader('Set-Cookie', `hebichess_session=${value}; Path=/; HttpOnly; SameSite=Lax${PRODUCTION ? '; Secure' : ''}`);
  return value;
}
function boardStart() { return ['rnbqkbnr','pppppppp','........','........','........','........','PPPPPPPP','RNBQKBNR'].map(row => row.split('')); }
function inside(row, col) { return row >= 0 && row < 8 && col >= 0 && col < 8; }
function color(piece) { return piece === '.' ? null : piece === piece.toUpperCase() ? 'w' : 'b'; }
function square(row, col) { return 'abcdefgh'[col] + (8 - row); }
function parse(value) { return /^[a-h][1-8]$/.test(value) ? [8 - Number(value[1]), value.charCodeAt(0) - 97] : null; }
function boardFen(board) { return board.map(row => { let out = '', empty = 0; for (const piece of row) { if (piece === '.') empty++; else { if (empty) out += empty, empty = 0; out += piece; } } return out + (empty || ''); }).join('/'); }
function positionKey(board, turn, castles, ep) { return `${boardFen(board)} ${turn} ${castles || '-'} ${ep || '-'}`; }
function attacks(board, row, col, by) {
  for (let y = 0; y < 8; y++) for (let x = 0; x < 8; x++) {
    const piece = board[y][x], type = piece.toLowerCase();
    if (color(piece) !== by) continue;
    const dr = row - y, dc = col - x, ar = Math.abs(dr), ac = Math.abs(dc);
    if (type === 'p' && dr === (by === 'w' ? -1 : 1) && ac === 1) return true;
    if (type === 'n' && ((ar === 2 && ac === 1) || (ar === 1 && ac === 2))) return true;
    if (type === 'k' && ar <= 1 && ac <= 1) return true;
    const diagonal = type === 'b' || type === 'q';
    const straight = type === 'r' || type === 'q';
    if ((diagonal && ar === ac && ar > 0) || (straight && ((ar === 0) !== (ac === 0)) && ar + ac > 0)) {
      const sy = Math.sign(dr), sx = Math.sign(dc); let yy = y + sy, xx = x + sx, clear = true;
      while (yy !== row || xx !== col) { if (board[yy][xx] !== '.') clear = false; yy += sy; xx += sx; }
      if (clear) return true;
    }
  }
  return false;
}
function inCheck(board, side) {
  for (let row = 0; row < 8; row++) for (let col = 0; col < 8; col++) if (board[row][col] === (side === 'w' ? 'K' : 'k')) return attacks(board, row, col, side === 'w' ? 'b' : 'w');
  return true;
}
function applyToBoard(board, move, side) {
  const next = copy(board), from = parse(move.from), to = parse(move.to), piece = next[from[0]][from[1]];
  next[from[0]][from[1]] = '.';
  next[to[0]][to[1]] = move.promotion ? (side === 'w' ? move.promotion.toUpperCase() : move.promotion.toLowerCase()) : piece;
  if (move.enPassant) next[side === 'w' ? to[0] + 1 : to[0] - 1][to[1]] = '.';
  if (move.castle === 'K' || move.castle === 'k') next[to[0]][5] = next[to[0]][7], next[to[0]][7] = '.';
  if (move.castle === 'Q' || move.castle === 'q') next[to[0]][3] = next[to[0]][0], next[to[0]][0] = '.';
  return next;
}
function pseudo(board, side, castles = '-', ep = '-') {
  const out = [];
  for (let row = 0; row < 8; row++) for (let col = 0; col < 8; col++) {
    const piece = board[row][col], type = piece.toLowerCase(); if (color(piece) !== side) continue;
    const add = (rr, cc, promotion = null, extra = {}) => { if (!inside(rr, cc) || color(board[rr][cc]) === side) return; out.push({from:square(row, col), to:square(rr, cc), promotion, ...extra}); };
    const pawnAdd = (rr, cc, extra = {}) => { if (rr === 0 || rr === 7) for (const promotion of ['q','r','b','n']) add(rr, cc, promotion, extra); else add(rr, cc, null, extra); };
    if (type === 'p') {
      const direction = side === 'w' ? -1 : 1;
      if (inside(row + direction, col) && board[row + direction][col] === '.') { pawnAdd(row + direction, col); if ((side === 'w' ? row === 6 : row === 1) && board[row + 2 * direction][col] === '.') add(row + 2 * direction, col); }
      for (const targetCol of [col - 1, col + 1]) if (inside(row + direction, targetCol)) {
        const target = square(row + direction, targetCol);
        if (color(board[row + direction][targetCol]) === (side === 'w' ? 'b' : 'w')) pawnAdd(row + direction, targetCol);
        else if (target === ep) pawnAdd(row + direction, targetCol, {enPassant:true});
      }
    } else if (type === 'n') for (const [dr, dc] of [[2,1],[2,-1],[-2,1],[-2,-1],[1,2],[1,-2],[-1,2],[-1,-2]]) add(row + dr, col + dc);
    else if (type === 'k') {
      for (let dr = -1; dr <= 1; dr++) for (let dc = -1; dc <= 1; dc++) if (dr || dc) add(row + dr, col + dc);
      if (side === 'w' && row === 7 && col === 4) {
        if (castles.includes('K') && board[7][7] === 'R' && board[7][5] === '.' && board[7][6] === '.' && !inCheck(board, side) && !attacks(board, 7, 5, 'b') && !attacks(board, 7, 6, 'b')) out.push({from:'e1',to:'g1',castle:'K'});
        if (castles.includes('Q') && board[7][0] === 'R' && board[7][1] === '.' && board[7][2] === '.' && board[7][3] === '.' && !inCheck(board, side) && !attacks(board, 7, 3, 'b') && !attacks(board, 7, 2, 'b')) out.push({from:'e1',to:'c1',castle:'Q'});
      }
      if (side === 'b' && row === 0 && col === 4) {
        if (castles.includes('k') && board[0][7] === 'r' && board[0][5] === '.' && board[0][6] === '.' && !inCheck(board, side) && !attacks(board, 0, 5, 'w') && !attacks(board, 0, 6, 'w')) out.push({from:'e8',to:'g8',castle:'k'});
        if (castles.includes('q') && board[0][0] === 'r' && board[0][1] === '.' && board[0][2] === '.' && board[0][3] === '.' && !inCheck(board, side) && !attacks(board, 0, 3, 'w') && !attacks(board, 0, 2, 'w')) out.push({from:'e8',to:'c8',castle:'q'});
      }
    } else {
      const directions = type === 'b' ? [[1,1],[1,-1],[-1,1],[-1,-1]] : type === 'r' ? [[1,0],[-1,0],[0,1],[0,-1]] : [[1,1],[1,-1],[-1,1],[-1,-1],[1,0],[-1,0],[0,1],[0,-1]];
      for (const [dr, dc] of directions) { let rr = row + dr, cc = col + dc; while (inside(rr, cc)) { if (board[rr][cc] === '.') add(rr, cc); else { if (color(board[rr][cc]) !== side) add(rr, cc); break; } rr += dr; cc += dc; } }
    }
  }
  return out.filter(move => !inCheck(applyToBoard(board, move, side), side));
}
function uci(move) { return move.from + move.to + (move.promotion || ''); }
function sanFor(board, move, side, castles, ep) {
  const piece = board[parse(move.from)[0]][parse(move.from)[1]], type = piece.toLowerCase();
  if (move.castle) return move.castle.toUpperCase() === 'K' ? 'O-O' : 'O-O-O';
  const capture = Boolean(board[parse(move.to)[0]][parse(move.to)[1]] !== '.' || move.enPassant);
  let san = type === 'p' ? (capture ? move.from[0] : '') : type === 'n' ? 'N' : type === 'b' ? 'B' : type === 'r' ? 'R' : type === 'q' ? 'Q' : 'K';
  if (type !== 'p') {
    const peers = pseudo(board, side, castles, ep).filter(candidate => candidate.to === move.to && candidate.from !== move.from && board[parse(candidate.from)[0]][parse(candidate.from)[1]].toLowerCase() === type);
    if (peers.length) san += peers.some(candidate => candidate.from[0] === move.from[0]) ? move.from[1] : move.from[0];
  }
  if (capture) san += 'x'; san += move.to; if (move.promotion) san += `=${move.promotion.toUpperCase()}`;
  const next = applyToBoard(board, move, side), nextSide = side === 'w' ? 'b' : 'w', replies = pseudo(next, nextSide, castles, ep);
  if (!replies.length && inCheck(next, nextSide)) san += '#'; else if (inCheck(next, nextSide)) san += '+';
  return san;
}
function updateCastles(castles, move, piece, captured) {
  let rights = castles;
  if (piece === 'K') rights = rights.replace(/[KQ]/g, ''); if (piece === 'k') rights = rights.replace(/[kq]/g, '');
  if (move.from === 'a1' || move.to === 'a1') rights = rights.replace('Q', ''); if (move.from === 'h1' || move.to === 'h1') rights = rights.replace('K', '');
  if (move.from === 'a8' || move.to === 'a8') rights = rights.replace('q', ''); if (move.from === 'h8' || move.to === 'h8') rights = rights.replace('k', '');
  return rights || '-';
}
function apply(move) {
  const from = parse(move.from), to = parse(move.to), piece = game.board[from[0]][from[1]], captured = game.board[to[0]][to[1]], side = game.turn;
  const san = sanFor(game.board, move, side, game.castles, game.ep);
  game.board = applyToBoard(game.board, move, side); game.castles = updateCastles(game.castles, move, piece, captured);
  game.ep = Math.abs(from[0] - to[0]) === 2 && piece.toLowerCase() === 'p' ? square((from[0] + to[0]) / 2, from[1]) : '-';
  game.halfmove = piece.toLowerCase() === 'p' || captured !== '.' || move.enPassant ? 0 : game.halfmove + 1;
  game.turn = side === 'w' ? 'b' : 'w'; game.moves.push(uci(move)); game.san.push(san); game.lastMove = uci(move);
  const key = positionKey(game.board, game.turn, game.castles, game.ep); game.positionHistory.push(key); game.repetitions[key] = (game.repetitions[key] || 0) + 1;
  game.history.push({uci: uci(move), san, board: copy(game.board), turn: game.turn, lastMove: game.lastMove, checkSquare:inCheck(game.board, game.turn) ? square(...findKing(game.board, game.turn)) : null});
}
function legal(value) {
  const promotion = value[4] ? value[4].toLowerCase() : null;
  return pseudo(game.board, game.turn, game.castles, game.ep).find(move => move.from === value.slice(0, 2) && move.to === value.slice(2, 4) && (promotion ? move.promotion === promotion : !move.promotion)) || null;
}
function promotionRequired(value) { return value.length === 4 && pseudo(game.board, game.turn, game.castles, game.ep).some(move => move.from === value.slice(0, 2) && move.to === value.slice(2, 4) && move.promotion); }
function capturedPieces() {
  const initial = {P:8,N:2,B:2,R:2,Q:1,p:8,n:2,b:2,r:2,q:1}, current = {};
  for (const row of game.board) for (const piece of row) if (piece !== '.') current[piece] = (current[piece] || 0) + 1;
  const taken = {w:[],b:[]}; for (const [piece, amount] of Object.entries(initial)) for (let i = Math.max(0, amount - (current[piece] || 0)); i; i--) taken[piece === piece.toUpperCase() ? 'b' : 'w'].push(piece.toLowerCase());
  return taken;
}
function insufficientMaterial(board = game.board) {
  const pieces = []; for (const row of board) for (const piece of row) if (piece !== '.' && piece.toLowerCase() !== 'k') pieces.push(piece.toLowerCase());
  return pieces.length === 0 || (pieces.length === 1 && (pieces[0] === 'b' || pieces[0] === 'n'));
}
function drawReason() {
  const key = game.positionHistory[game.positionHistory.length - 1];
  if (game.repetitions[key] >= 3) return 'threefold repetition';
  if (game.halfmove >= 100) return '50-move rule';
  if (insufficientMaterial()) return 'insufficient material';
  return null;
}
function currentState(sessionId) {
  if (!game) return {active:false, role:'spectator', isPlayer:false, result:null, termination:null};
  return {active:true, role:sessionId === game.playerSessionId ? 'player' : 'spectator', isPlayer:sessionId === game.playerSessionId, playerColor:game.playerColor, board:game.board, currentFen:`${boardFen(game.board)} ${game.turn} ${game.castles} ${game.ep} ${game.halfmove} ${Math.floor(game.moves.length / 2) + 1}`, moves:game.moves, san:game.san, history:game.history.map(item => ({uci:item.uci, san:item.san, board:item.board, turn:item.turn, lastMove:item.lastMove, checkSquare:item.checkSquare || null})), turn:game.turn, engineThinking:game.engineThinking, result:game.result, termination:game.termination, lastMove:game.lastMove, checkSquare:inCheck(game.board, game.turn) ? square(...findKing(game.board, game.turn)) : null, depth:game.depth, evaluation:game.evaluation, nodes:game.nodes || 0, capturedPieces:capturedPieces(), legalMoves:pseudo(game.board, game.turn, game.castles, game.ep).map(uci), startedAt:game.startedAt};
}
function findKing(board, side) { for (let row = 0; row < 8; row++) for (let col = 0; col < 8; col++) if (board[row][col] === (side === 'w' ? 'K' : 'k')) return [row, col]; return [0, 0]; }
function emit(type, data = null) { for (const [response, sessionId] of clients) { const payload = data || currentState(sessionId); response.write(`event: ${type}\ndata: ${JSON.stringify(payload)}\n\n`); } }
function end(result, termination) {
  if (!game) return;
  game.result = result; game.termination = termination; game.engineThinking = false;
  const endedAt = new Date().toISOString();
  fs.mkdirSync(path.dirname(DATA), {recursive:true}); let old = []; try { old = JSON.parse(fs.readFileSync(DATA)); } catch {}
  old.push({startTime:game.startedAt, endTime:endedAt, playerColor:game.playerColor, moves:game.moves, result, termination}); fs.writeFileSync(DATA, JSON.stringify(old, null, 2));
  emit('gameOver'); if (engine) { engine.kill(); engine = null; } game = null; emit('state');
}
function terminalAfterMove() {
  const moves = pseudo(game.board, game.turn, game.castles, game.ep);
  if (!moves.length) return inCheck(game.board, game.turn) ? [game.turn === 'w' ? '0-1' : '1-0', 'checkmate'] : ['1/2-1/2', 'stalemate'];
  const draw = drawReason(); return draw ? ['1/2-1/2', draw] : null;
}
function engineGo() {
  if (!game || game.result || game.turn === game.playerColor) return;
  game.engineThinking = true; emit('engineThinking');
  if (!engine) {
    engine = spawn(ENGINE, [], {stdio:['pipe','pipe','pipe']});
    engine.stdout.on('data', data => { for (const line of data.toString().split(/\r?\n/)) { if (line.startsWith('info ')) { const depth = line.match(/\bdepth (\d+)/), score = line.match(/\bscore cp (-?\d+)/), nodes = line.match(/\bnodes (\d+)/); if (game) { game.depth = depth ? Number(depth[1]) : game.depth; game.evaluation = score ? Number(score[1]) / 100 : game.evaluation; game.nodes = nodes ? Number(nodes[1]) : game.nodes; emit('engineInfo'); } } if (line.startsWith('bestmove ') && game && game.engineThinking) { const move = legal(line.split(/\s+/)[1]); if (move) { apply(move); game.engineThinking = false; emit('move'); const terminal = terminalAfterMove(); if (terminal) end(...terminal); else engineGo(); } } } });
    engine.on('error', () => { if (game) { game.engineThinking = false; end('0-1', 'engine-error'); } }); engine.stdin.write('uci\nisready\n');
  }
  setTimeout(() => { if (engine && game) engine.stdin.write(`position startpos moves ${game.moves.join(' ')}\ngo depth ${DEPTH}\n`); }, 150);
}
function start(session, colorChoice) {
  if (game) return false;
  const playerColor = colorChoice === 'random' ? (Math.random() < .5 ? 'w' : 'b') : colorChoice === 'black' ? 'b' : 'w';
  const board = boardStart(), key = positionKey(board, 'w', 'KQkq', '-');
  game = {active:true, playerSessionId:session, playerColor, board, castles:'KQkq', ep:'-', halfmove:0, moves:[], san:[], turn:'w', engineThinking:false, result:null, termination:null, lastMove:null, depth:0, evaluation:0, nodes:0, startedAt:new Date().toISOString(), repetitions:{[key]:1}, positionHistory:[key], history:[{uci:null, san:null, board:copy(board), turn:'w', lastMove:null, checkSquare:null}]};
  emit('state'); if (playerColor === 'b') engineGo(); return true;
}
function body(req) { return new Promise((resolve, reject) => { let text = ''; req.on('data', data => text += data); req.on('end', () => { try { resolve(JSON.parse(text || '{}')); } catch { reject(new Error('invalid json')); } }); }); }
function json(response, status, data) { response.writeHead(status, {'Content-Type':'application/json'}); response.end(JSON.stringify(data)); }
function handle(req, response, session) {
  const url = new URL(req.url, 'http://localhost');
  if (url.pathname === '/events') { response.writeHead(200, {'Content-Type':'text/event-stream','Cache-Control':'no-cache','Connection':'keep-alive'}); clients.set(response, session); response.write(`event: state\ndata: ${JSON.stringify(currentState(session))}\n\n`); req.on('close', () => clients.delete(response)); return; }
  if (url.pathname === '/api/state') return json(response, 200, currentState(session));
  if (req.method === 'POST' && url.pathname === '/api/start') return body(req).then(value => start(session, value.color || 'random') ? json(response, 200, currentState(session)) : json(response, 409, {error:'active game'}));
  if (req.method === 'POST' && url.pathname === '/api/move') return body(req).then(value => { if (!game) return json(response, 409, {error:'no active game'}); if (session !== game.playerSessionId) return json(response, 403, {error:'spectator'}); if (game.engineThinking || game.turn !== game.playerColor) return json(response, 409, {error:'not your turn'}); const requested = value.move || ''; if (promotionRequired(requested)) return json(response, 422, {error:'promotion required', promotionRequired:true}); const move = legal(requested); if (!move) return json(response, 422, {error:'illegal move'}); apply(move); emit('move'); const terminal = terminalAfterMove(); if (terminal) end(...terminal); else engineGo(); return json(response, 200, currentState(session)); });
  if (req.method === 'POST' && url.pathname === '/api/resign') { if (!game || session !== game.playerSessionId) return json(response, 403, {error:'not player'}); end(game.playerColor === 'w' ? '0-1' : '1-0', 'resignation'); return json(response, 200, {ok:true}); }
  if (url.pathname === '/' || url.pathname.startsWith('/public/')) { const file = url.pathname === '/' ? 'index.html' : url.pathname.slice(8), filePath = path.join(root, 'public', file); if (!filePath.startsWith(PUBLIC_ROOT) || !fs.existsSync(filePath)) return json(response, 404, {error:'not found'}); response.writeHead(200, {'Content-Type':files[path.extname(filePath)] || 'text/plain'}); return fs.createReadStream(filePath).pipe(response); }
  return json(response, 404, {error:'not found'});
}
const server = http.createServer((req, response) => { const session = cookie(req, response); serial = serial.then(() => handle(req, response, session)).catch(error => json(response, 500, {error:error.message})); });
if (require.main === module) server.listen(PORT, '127.0.0.1', () => console.log(`HebiChess web listening on http://127.0.0.1:${PORT}`));
module.exports = {server, start, currentState, state:currentState, legal, apply, emit, pseudo, positionKey, drawReason, insufficientMaterial, terminalAfterMove, boardStart, setGame(value) { game = value; }, getGame:() => game};
