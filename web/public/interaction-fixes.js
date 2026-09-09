/* Interaction hardening based on the stable pointer/premove patterns used by yddfhbh/chess. */
(() => {
  const {squareFromClientPoint, reconcilePartialPremove} = HebiChessUi;

  function boardSquareAt(clientX, clientY) {
    const board = $('board');
    if (!board) return null;
    return squareFromClientPoint(clientX, clientY, board.getBoundingClientRect(), flipped);
  }

  function clearDragMoveHints() {
    document.querySelectorAll('.drag-legal,.drag-capture').forEach(el => {
      el.classList.remove('drag-legal', 'drag-capture');
    });
  }

  function applyDragMoveHints() {
    clearDragMoveHints();
    legalDestinations.forEach(destination => {
      const square = document.querySelector(`.square[data-square="${destination}"]`);
      if (!square) return;
      square.classList.add(pieceAt(destination) === '.' ? 'drag-legal' : 'drag-capture');
    });
  }

  function reconcilePartialSelection() {
    const decision = reconcilePartialPremove({
      mode: selectedInteractionMode,
      from: selectedSquare,
      hasQueuedPremove: !!premove,
      playerTurn: isPlayer() && !S.result && S.turn === S.playerColor,
      sourceIsOwnPiece: !!selectedSquare && ownPiece(selectedSquare),
      legalMoves: S.legalMoves || []
    });

    if (decision.action === 'clear') {
      clearInteraction();
      return;
    }
    if (decision.action === 'live') {
      cleanupDrag();
      selectedSquare = decision.from;
      selectedPiece = pieceAt(decision.from);
      selectedInteractionMode = 'live';
      legalDestinations = decision.legalDestinations;
    }
  }

  receive = function receiveInteractionSafe(data, eventType) {
    if (!data.active && S.result) return;
    if (data.result && eventType === 'gameOver') {
      clearInteraction();
      premove = null;
      S = data;
      viewIndex = -1;
      render();
      showGameOver(data);
      return;
    }

    const wasLive = live();
    const oldLength = S.moves?.length || 0;
    S = data;

    if (!isPlayer()) {
      clearInteraction();
    } else if (data.active && data.turn === data.playerColor) {
      reconcilePartialSelection();
    }

    if (!wasLive && (data.moves?.length || 0) > oldLength) latestState = data;
    else if (wasLive) viewIndex = -1;

    if (data.active && data.turn === data.playerColor && data.isPlayer) maybePremove();
    render();
  };

  activateDrag = function activateDragStable(event) {
    if (!drag || drag.dragging) return;
    drag.dragging = true;
    selectedSquare = drag.from;
    selectedPiece = drag.piece;
    selectedInteractionMode = S.turn === S.playerColor ? 'live' : 'premove';
    legalDestinations = selectedInteractionMode === 'live' ? normalTargets(drag.from) : relaxedTargets(drag.from);
    applyDragMoveHints();

    const sourcePiece = drag.source?.querySelector?.('.piece');
    if (sourcePiece) sourcePiece.style.opacity = '.28';
    else if (drag.source) drag.source.style.opacity = '.28';

    const boardRect = $('board').getBoundingClientRect();
    const ghost = document.createElement('div');
    ghost.className = 'drag-ghost';
    ghost.style.width = `${boardRect.width / 8}px`;
    ghost.style.height = `${boardRect.height / 8}px`;
    ghost.innerHTML = pieceSvg(drag.piece);
    document.body.append(ghost);
    drag.ghost = ghost;
    moveGhost(event);
  };

  moveGhost = function moveGhostStable(event) {
    if (!drag?.ghost) return;
    const size = drag.ghost.getBoundingClientRect();
    drag.ghost.style.left = `${event.clientX - size.width / 2}px`;
    drag.ghost.style.top = `${event.clientY - size.height / 2}px`;
  };

  cleanupDrag = function cleanupDragStable() {
    clearDragMoveHints();
    if (!drag) return;
    try { drag.source?.releasePointerCapture?.(drag.pointerId); } catch {}
    const sourcePiece = drag.source?.querySelector?.('.piece');
    if (sourcePiece) sourcePiece.style.opacity = '';
    if (drag.source) drag.source.style.opacity = '';
    drag.ghost?.remove();
    document.querySelectorAll('.drag-target').forEach(el => el.classList.remove('drag-target'));
    document.removeEventListener('pointermove', moveDrag);
    drag = null;
  };

  moveDrag = function moveDragStable(event) {
    if (!drag || event.pointerId !== drag.pointerId) return;
    if (event.cancelable) event.preventDefault();
    const distance = Math.hypot(event.clientX - drag.startX, event.clientY - drag.startY);
    if (!drag.dragging && distance >= 6) activateDrag(event);
    if (!drag.dragging) return;

    moveGhost(event);
    document.querySelectorAll('.drag-target').forEach(el => el.classList.remove('drag-target'));
    const destination = boardSquareAt(event.clientX, event.clientY);
    if (!destination || !legalDestinations.includes(destination)) return;
    document.querySelector(`.square[data-square="${destination}"]`)?.classList.add('drag-target');
  };

  endDrag = function endDragStable(event) {
    if (!drag || event.pointerId !== drag.pointerId) return;
    const wasDragging = drag.dragging;
    const destination = wasDragging ? boardSquareAt(event.clientX, event.clientY) : null;
    cleanupDrag();
    if (!wasDragging) return;

    // Swallow only the synthetic click generated by this pointer sequence.
    suppressClick = true;
    setTimeout(() => { suppressClick = false; }, 0);

    if (!destination || !chooseDestination(destination)) {
      clearInteraction();
      render();
    }
  };

  cancelDrag = function cancelDragStable(event) {
    if (!drag || event.pointerId !== drag.pointerId) return;
    const changed = drag.dragging;
    cleanupDrag();
    if (changed) {
      suppressClick = true;
      setTimeout(() => { suppressClick = false; }, 0);
      clearInteraction();
      render();
    }
  };
})();
