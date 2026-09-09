# NNUE backend follow-up design

`evaluate(const Board&)` remains the HCE-only search entry point.  It calls
`evaluate_hce(const Board&)`; this makes the existing hot path independent of
UCI mode selection.  `evaluate(const Board&, EvalMode)` is the explicit
backend API for setup and tests.  It returns no value for NNUE until a
validated network is available.

## Incremental accumulator hooks

The future accumulator belongs beside the board state, but it should not be
added until a network format fixes the accumulator dimensions and layout.
`Board::make_move()` in `src/chess/board.cpp` is the single forward hook: emit
piece-square removals and additions before each `set_piece()` call, recording
the inverse operations in `UndoState` (or an evaluator-owned parallel undo
stack).  `Board::unmake_move()` restores the same saved accumulator state
after it restores pieces.  A null move needs no feature change, only its usual
side-to-move state handling.

The exact deltas are: ordinary move (remove `from`, add `to`); capture
(remove captured `to`, then moving delta); en passant (remove captured pawn at
`captured_square`, then moving delta); promotion (remove pawn at `from`, add
promoted piece at `to`); promotion capture (also remove target); castling
(king delta plus rook `a/h` to `d/f` delta); and any king move (the king
square itself changes the king-relative feature plane, so rebuild that
perspective's accumulator rather than trying to transform every feature).

## First representation

Start with a HalfKP-style king-relative encoding: for each perspective's king
square, encode every non-king piece by color, type, and square.  A compact
baseline uses `2 * 6 * 64 * 64 = 49,152` binary input features if both king
perspectives and six piece types are retained (king features can be omitted,
reducing the piece-type term to five).  It is simple, has proven king-safety
expressiveness, and supports incremental updates for all non-king moves.
Its costs are a relatively large sparse first layer and a full perspective
refresh on king moves.  Do not put root style or aggression terms into these
features: the network output must remain the objective score consumed by
search, with HebiChess root policy applied afterwards.
