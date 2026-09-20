# HebiChess opening-book format (v1)

The Phase 7-4A `.hebibook` file is little-endian and contains no pointers:

* Header: `HEBIBOOK` (8 bytes), version/position count/move count (`uint32`),
  `max_book_ply` and `min_move_count` (`uint16`).
* Position table: sorted unique `uint64 key`, `uint32 move_offset`,
  `uint16 move_count`, and a zero `uint16 reserved` field (16 bytes each).
* Move table: packed move (`uint16`), zero `uint16 reserved`, and `uint32 weight`
  (8 bytes each).

Packed moves use bits 0--5 for the source square, 6--11 for the target square,
and 12--14 for promotion: `0=normal`, `1=knight`, `2=bishop`, `3=rook`,
`4=queen`. Bit 15 is reserved and must be zero.

The key uses HebiChess's existing SplitMix64-derived Zobrist sequence. It uses
the python-chess FEN first-four-fields convention: the en-passant component is
included only when a legal en-passant move exists. Runtime lookup still filters
every stored move against the current legal move list.
