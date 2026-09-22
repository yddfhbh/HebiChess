const START_BOARD = [
  'rnbqkbnr', 'pppppppp', '........', '........',
  '........', '........', 'PPPPPPPP', 'RNBQKBNR'
].join('').split('');
const FILES = 'abcdefgh';
const KNIGHT = [[-2,-1],[-2,1],[-1,-2],[-1,2],[1,-2],[1,2],[2,-1],[2,1]];
const KING = [[-1,-1],[-1,0],[-1,1],[0,-1],[0,1],[1,-1],[1,0],[1,1]];
const ROOK = [[-1,0],[1,0],[0,-1],[0,1]];
const BISHOP = [[-1,-1],[-1,1],[1,-1],[1,1]];

export function square(index) { return `${FILES[index % 8]}${8 - Math.floor(index / 8)}`; }
export function indexOfSquare(value) {
  if (!/^[a-h][1-8]$/.test(value)) return -1;
  return (8 - Number(value[1])) * 8 + FILES.indexOf(value[0]);
}
export function opposite(color) { return color === 'w' ? 'b' : 'w'; }
export function colorOf(piece) { return piece === piece?.toUpperCase() ? 'w' : 'b'; }
export function pieceType(piece) { return piece?.toLowerCase(); }

export function fromFen(fen) {
  const fields = String(fen).trim().split(/\s+/);
  if (fields.length < 4) throw new Error('Invalid FEN');
  const board = [];
  for (const row of fields[0].split('/')) {
    if (row.length === 0) throw new Error('Invalid FEN rank');
    for (const char of row) {
      if (/^[1-8]$/.test(char)) board.push(...Array(Number(char)).fill('.'));
      else if (/^[prnbqkPRNBQK]$/.test(char)) board.push(char);
      else throw new Error('Invalid FEN piece');
    }
  }
  if (board.length !== 64 || !board.includes('K') || !board.includes('k')) throw new Error('Invalid FEN board');
  return { board, turn: fields[1], castling: fields[2] === '-' ? '' : fields[2], enPassant: fields[3],
    halfmove: Number(fields[4] || 0), fullmove: Number(fields[5] || 1), moves: [], san: [], captured: [], lastMove: null };
}

export function newGame() { return fromFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'); }
export function toFen(state) {
  const ranks = [];
  for (let row = 0; row < 8; row++) {
    let rank = '', empty = 0;
    for (let col = 0; col < 8; col++) {
      const piece = state.board[row * 8 + col];
      if (piece === '.') empty++;
      else { if (empty) rank += empty; empty = 0; rank += piece; }
    }
    if (empty) rank += empty;
    ranks.push(rank);
  }
  return `${ranks.join('/')} ${state.turn} ${state.castling || '-'} ${state.enPassant || '-'} ${state.halfmove} ${state.fullmove}`;
}

function inBounds(row, col) { return row >= 0 && row < 8 && col >= 0 && col < 8; }
function attacked(state, target, byColor) {
  const row = Math.floor(target / 8), col = target % 8;
  const pawnRow = row + (byColor === 'w' ? 1 : -1);
  for (const dc of [-1, 1]) if (inBounds(pawnRow, col + dc) && state.board[pawnRow * 8 + col + dc] === (byColor === 'w' ? 'P' : 'p')) return true;
  for (const [dr, dc] of KNIGHT) if (inBounds(row + dr, col + dc) && state.board[(row + dr) * 8 + col + dc] === (byColor === 'w' ? 'N' : 'n')) return true;
  for (const [dr, dc] of KING) if (inBounds(row + dr, col + dc) && state.board[(row + dr) * 8 + col + dc] === (byColor === 'w' ? 'K' : 'k')) return true;
  for (const [directions, types] of [[ROOK, 'rq'], [BISHOP, 'bq']]) for (const [dr, dc] of directions) {
    let r = row + dr, c = col + dc;
    while (inBounds(r, c)) { const piece = state.board[r * 8 + c]; if (piece !== '.') { if (colorOf(piece) === byColor && types.includes(pieceType(piece))) return true; break; } r += dr; c += dc; }
  }
  return false;
}
function kingInCheck(state, color) { const king = state.board.indexOf(color === 'w' ? 'K' : 'k'); return king < 0 || attacked(state, king, opposite(color)); }

function pseudoMoves(state, from) {
  const piece = state.board[from], type = pieceType(piece), color = colorOf(piece);
  if (piece === '.' || color !== state.turn) return [];
  const row = Math.floor(from / 8), col = from % 8, moves = [];
  const add = (to, extra = {}) => { if (!inBounds(Math.floor(to / 8), to % 8)) return; const target = state.board[to]; if (target === '.' || colorOf(target) !== color) moves.push({ from, to, promotion: extra.promotion, castle: extra.castle, enPassant: extra.enPassant }); };
  if (type === 'p') {
    const dir = color === 'w' ? -1 : 1, start = color === 'w' ? 6 : 1, promotion = color === 'w' ? 0 : 7;
    const one = (row + dir) * 8 + col;
    if (inBounds(row + dir, col) && state.board[one] === '.') {
      if (row + dir === promotion) for (const p of 'qrbn') add(one, { promotion: p }); else add(one);
      const two = (row + 2 * dir) * 8 + col; if (row === start && state.board[two] === '.') add(two);
    }
    for (const dc of [-1, 1]) { const r = row + dir, c = col + dc; if (!inBounds(r, c)) continue; const to = r * 8 + c, target = state.board[to];
      if (target !== '.' && colorOf(target) !== color) { if (r === promotion) for (const p of 'qrbn') add(to, { promotion: p }); else add(to); }
      if (square(to) === state.enPassant) add(to, { enPassant: true });
    }
  } else if (type === 'n' || type === 'k') {
    for (const [dr, dc] of type === 'n' ? KNIGHT : KING) { const r = row + dr, c = col + dc; if (inBounds(r, c)) add(r * 8 + c); }
    if (type === 'k' && !kingInCheck(state, color)) {
      const rights = color === 'w' ? [['K', 63, 62, 61], ['Q', 56, 58, 59]] : [['k', 7, 6, 5], ['q', 0, 2, 3]];
      for (const [right, rook, to, transit] of rights) if (state.castling.includes(right) && state.board[rook] === (color === 'w' ? 'R' : 'r') && state.board[transit] === '.' && state.board[to] === '.' && !attacked(state, transit, opposite(color)) && !attacked(state, to, opposite(color))) add(to, { castle: right });
    }
  } else {
    const directions = type === 'r' ? ROOK : type === 'b' ? BISHOP : [...ROOK, ...BISHOP];
    for (const [dr, dc] of directions) { let r = row + dr, c = col + dc; while (inBounds(r, c)) { const to = r * 8 + c, target = state.board[to]; if (target === '.') moves.push({ from, to }); else { if (colorOf(target) !== color) moves.push({ from, to }); break; } r += dr; c += dc; } }
  }
  return moves;
}

function applyUnchecked(state, move) {
  const next = { ...state, board: [...state.board], moves: [...state.moves], san: [...state.san], captured: [...state.captured] };
  const piece = next.board[move.from], color = colorOf(piece), target = next.board[move.to];
  next.board[move.from] = '.'; next.board[move.to] = move.promotion ? (color === 'w' ? move.promotion.toUpperCase() : move.promotion) : piece;
  if (move.enPassant) { const capturedAt = move.to + (color === 'w' ? 8 : -8); next.captured.push(next.board[capturedAt].toLowerCase()); next.board[capturedAt] = '.'; }
  else if (target !== '.') next.captured.push(target.toLowerCase());
  if (move.castle) { const rookFrom = move.castle.toLowerCase() === 'k' ? move.from + 3 : move.from - 4, rookTo = move.castle.toLowerCase() === 'k' ? move.from + 1 : move.from - 1; next.board[rookTo] = next.board[rookFrom]; next.board[rookFrom] = '.'; }
  const rights = new Set(next.castling); if (pieceType(piece) === 'k') { rights.delete(color === 'w' ? 'K' : 'k'); rights.delete(color === 'w' ? 'Q' : 'q'); }
  if (pieceType(piece) === 'r') { if (move.from === 0) rights.delete('q'); if (move.from === 7) rights.delete('k'); if (move.from === 56) rights.delete('Q'); if (move.from === 63) rights.delete('K'); }
  if (target !== '.' && pieceType(target) === 'r') { if (move.to === 0) rights.delete('q'); if (move.to === 7) rights.delete('k'); if (move.to === 56) rights.delete('Q'); if (move.to === 63) rights.delete('K'); }
  next.castling = [...rights].join(''); next.enPassant = Math.abs(move.to - move.from) === 16 ? square((move.to + move.from) / 2) : '-';
  next.halfmove = pieceType(piece) === 'p' || target !== '.' || move.enPassant ? 0 : state.halfmove + 1;
  next.fullmove = state.fullmove + (color === 'b' ? 1 : 0); next.turn = opposite(color); next.lastMove = move;
  return next;
}
export function legalMoves(state, from = null) {
  const candidates = from === null ? state.board.flatMap((piece, i) => piece !== '.' && colorOf(piece) === state.turn ? pseudoMoves(state, i) : []) : pseudoMoves(state, from);
  return candidates.filter(move => !kingInCheck(applyUnchecked(state, move), state.turn));
}
function sanFor(state, move, legal) {
  const piece = state.board[move.from], type = pieceType(piece), capture = state.board[move.to] !== '.' || move.enPassant;
  if (move.castle) return move.castle.toLowerCase() === 'k' ? 'O-O' : 'O-O-O';
  let san = type === 'p' ? (capture ? FILES[move.from % 8] : '') : type.toUpperCase();
  if (type !== 'p') { const peers = legal.filter(m => m.to === move.to && m.from !== move.from && pieceType(state.board[m.from]) === type); if (peers.length) san += FILES[move.from % 8]; if (peers.some(m => Math.floor(m.from / 8) === Math.floor(move.from / 8))) san = san.replace(FILES[move.from % 8], String(8 - Math.floor(move.from / 8))); }
  if (capture) san += 'x'; san += square(move.to); if (move.promotion) san += `=${move.promotion.toUpperCase()}`;
  const next = applyUnchecked(state, move); if (kingInCheck(next, next.turn)) san += legalMoves(next).length ? '+' : '#'; return san;
}
export function play(state, uci) {
  const from = indexOfSquare(uci.slice(0, 2)), to = indexOfSquare(uci.slice(2, 4)), promotion = uci[4]?.toLowerCase();
  const legal = legalMoves(state); const move = legal.find(candidate => candidate.from === from && candidate.to === to && (candidate.promotion || !promotion || candidate.promotion === promotion));
  if (!move) return null;
  const next = applyUnchecked(state, move); next.moves.push(`${square(move.from)}${square(move.to)}${move.promotion || ''}`); next.san.push(sanFor(state, move, legal)); return next;
}
export function status(state) { const moves = legalMoves(state); if (moves.length) return { status: kingInCheck(state, state.turn) ? 'check' : 'ongoing', result: null }; if (kingInCheck(state, state.turn)) return { status: 'checkmate', result: state.turn === 'w' ? '0-1' : '1-0' }; return { status: 'stalemate', result: '1/2-1/2' }; }
export function boardFromFen(fen) { return fromFen(fen).board; }
