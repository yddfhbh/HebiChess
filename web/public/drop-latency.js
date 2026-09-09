/* Immediate visual feedback for live drag drops while the server remains authoritative. */
(() => {
  let queuedDropPreview = null;
  let pendingDropPreview = null;

  function clearOptimisticDrop() {
    if (!pendingDropPreview) return;
    pendingDropPreview.sourcePiece?.style.removeProperty('visibility');
    pendingDropPreview.preview?.remove();
    pendingDropPreview = null;
  }

  function showOptimisticDrop(preview) {
    clearOptimisticDrop();
    if (!preview) return;
    const sourceSquare = document.querySelector(`.square[data-square="${preview.from}"]`);
    const destinationSquare = document.querySelector(`.square[data-square="${preview.to}"]`);
    if (!destinationSquare) return;

    const sourcePiece = sourceSquare?.querySelector?.('.piece') || null;
    if (sourcePiece) sourcePiece.style.visibility = 'hidden';

    const overlay = document.createElement('div');
    overlay.className = 'drop-preview';
    overlay.innerHTML = pieceSvg(preview.piece);
    destinationSquare.appendChild(overlay);
    pendingDropPreview = {sourcePiece, preview:overlay};
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

  sendMove = function sendMoveImmediate(move) {
    if (requestInFlight) {
      queuedDropPreview = null;
      return;
    }
    requestInFlight = true;
    clearInteraction();
    if (queuedDropPreview && move.startsWith(queuedDropPreview.from + queuedDropPreview.to)) {
      showOptimisticDrop(queuedDropPreview);
    }
    queuedDropPreview = null;

    api('/api/move',{move}).then(data=>{
      clearOptimisticDrop();
      S=data;
      viewIndex=-1;
      render();
    }).catch(error=>{
      clearOptimisticDrop();
      premove=null;
      toast(error.message);
      render();
    }).finally(()=>{requestInFlight=false});
  };

  const previousReceive = receive;
  receive = function receiveWithDropPreviewCleanup(data, eventType) {
    clearOptimisticDrop();
    return previousReceive(data, eventType);
  };
})();
