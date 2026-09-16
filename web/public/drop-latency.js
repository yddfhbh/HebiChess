/* Immediate visual feedback for live drag drops while the browser GameState remains authoritative. */
(() => {
  let queuedDropPreview = null;
  let pendingDropPreview = null;

  function clearOptimisticDrop() {
    if (!pendingDropPreview) return;
    pendingDropPreview.sourcePiece?.style.removeProperty('visibility');
    pendingDropPreview.destinationPiece?.style.removeProperty('visibility');
    pendingDropPreview.preview?.remove();
    pendingDropPreview = null;
  }

  function clearImmediateMoveMarkers() {
    document.querySelectorAll(
      '.selected,.legal,.capture,.drag-legal,.drag-capture,.drag-target'
    ).forEach(element => {
      element.classList.remove(
        'selected',
        'legal',
        'capture',
        'drag-legal',
        'drag-capture',
        'drag-target'
      );
    });
  }

  function showOptimisticDrop(preview) {
    clearOptimisticDrop();
    if (!preview) return;
    const sourceSquare = document.querySelector(`.square[data-square="${preview.from}"]`);
    const destinationSquare = document.querySelector(`.square[data-square="${preview.to}"]`);
    if (!destinationSquare) return;

    const sourcePiece = sourceSquare?.querySelector?.('.piece') || null;
    const destinationPiece = destinationSquare.querySelector?.('.piece') || null;

    if (sourcePiece) sourcePiece.style.visibility = 'hidden';
    if (destinationPiece) destinationPiece.style.visibility = 'hidden';

    const overlay = document.createElement('div');
    overlay.className = 'drop-preview';
    overlay.innerHTML = pieceSvg(preview.piece);
    destinationSquare.appendChild(overlay);
    pendingDropPreview = {
      sourcePiece,
      destinationPiece,
      preview:overlay
    };
  }

  const previousEndDrag = endDrag;
  endDrag = function endDragWithImmediateDrop(event) {
    if (!drag || event.pointerId !== drag.pointerId) return;
    const destination = drag.dragging ? boardSquareAtForDropLatency(event.clientX, event.clientY) : null;
    if (
      destination &&
      selectedInteractionMode === 'live' &&
      legalDestinations.includes(destination) &&
      promotionVariants(drag.from, destination).length === 0
    ) {
      queuedDropPreview = {from:drag.from, to:destination, piece:drag.piece};
    } else {
      queuedDropPreview = null;
    }
    previousEndDrag(event);
  };

  function boardSquareAtForDropLatency(clientX, clientY) {
    const board = $('board');
    if (!board) return null;
    return HebiChessUi.squareFromClientPoint(clientX, clientY, board.getBoundingClientRect(), flipped);
  }

  const canonicalSendMove = sendMove;
  sendMove = function sendMoveImmediate(move) {
    if (requestInFlight) {
      queuedDropPreview = null;
      return;
    }
    let preview = null;

    if (
      queuedDropPreview &&
      move.startsWith(queuedDropPreview.from + queuedDropPreview.to)
    ) {
      preview = queuedDropPreview;
    } else if (typeof move === 'string' && move.length >= 4 && move.length === 4) {
      const from = move.slice(0, 2);
      const to = move.slice(2, 4);
      const piece = pieceAt(from);

      if (piece && piece !== '.') {
        preview = {from, to, piece};
      }
    }

    if (preview) showOptimisticDrop(preview);
    clearImmediateMoveMarkers();
    queuedDropPreview = null;

    console.debug?.('[Phase2][drop-latency] delegating move to canonical sendMove', {move});
    canonicalSendMove(move);
  };

  const previousReceive = receive;
  receive = function receiveWithDropPreviewCleanup(data, eventType) {
    clearOptimisticDrop();
    return previousReceive(data, eventType);
  };
})();
