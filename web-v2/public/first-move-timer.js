(function() {
    var FIRST_MOVE_TIMEOUT_MS = 15000;

    var whiteFirstMoveDeadlineAt = null;
    var blackFirstMoveDeadlineAt = null;

    function isFirstMoveTimerEnabled() {
        return typeof isOnlineMode === 'function' && isOnlineMode();
    }

    function hasColorMadeMove(color) {
        for (var i = 0; i < moveHistory.length; i++) {
            if (moveHistory[i] && moveHistory[i].color === color) return true;
        }
        return color === 'white' ? !!firstMoveMade : false;
    }

    function getFirstMoveDeadline(color) {
        if (!isFirstMoveTimerEnabled()) return null;
        if (color === 'white') return hasColorMadeMove('white') ? null : whiteFirstMoveDeadlineAt;
        if (color === 'black') return hasColorMadeMove('black') ? null : blackFirstMoveDeadlineAt;
        return null;
    }

    function clearFirstMoveDeadlines() {
        whiteFirstMoveDeadlineAt = null;
        blackFirstMoveDeadlineAt = null;
    }

    function syncFirstMoveDeadlinesToCurrentState() {
        var whiteMoved = hasColorMadeMove('white');
        var blackMoved = hasColorMadeMove('black');

        if (!isFirstMoveTimerEnabled() || gameOver) {
            clearFirstMoveDeadlines();
            return;
        }

        if (!whiteMoved) {
            if (typeof whiteFirstMoveDeadlineAt !== 'number') {
                whiteFirstMoveDeadlineAt = Date.now() + FIRST_MOVE_TIMEOUT_MS;
            }
            blackFirstMoveDeadlineAt = null;
            return;
        }

        whiteFirstMoveDeadlineAt = null;

        if (!blackMoved) {
            if (typeof blackFirstMoveDeadlineAt !== 'number') {
                blackFirstMoveDeadlineAt = Date.now() + FIRST_MOVE_TIMEOUT_MS;
            }
            return;
        }

        blackFirstMoveDeadlineAt = null;
    }

    function shouldRunTimerForCurrentTurn() {
        if (gameOver) return false;
        if (typeof isOnlineMode === 'function' && isOnlineMode() && typeof isLocalOnlineTurn === 'function' && !isLocalOnlineTurn()) {
            return false;
        }
        return !gameSetting.unlimited || !!getFirstMoveDeadline(currentTurn);
    }

    function formatFirstMoveCountdown(deadlineAt) {
        var secondsLeft = Math.max(0, Math.ceil((deadlineAt - Date.now()) / 1000));
        return '\uCCAB \uC218 ' + secondsLeft + '\uCD08';
    }

    function updateFirstMoveTimerDisplay() {
        ['white', 'black'].forEach(function(color) {
            var el = document.getElementById(color + '-first-move-timer');
            if (!el) return;

            el.classList.remove('expired', 'waiting');

            if (!isFirstMoveTimerEnabled()) {
                el.textContent = '';
                el.style.display = 'none';
                return;
            }

            if (gameOver || hasColorMadeMove(color)) {
                el.textContent = '';
                el.style.display = 'none';
                return;
            }

            var deadlineAt = getFirstMoveDeadline(color);
            if (deadlineAt) {
                el.textContent = formatFirstMoveCountdown(deadlineAt);
                if (Date.now() >= deadlineAt) el.classList.add('expired');
            } else {
                el.textContent = '\uCCAB \uC218 \uB300\uAE30 15\uCD08';
                el.classList.add('waiting');
            }

            el.style.display = 'block';
        });
    }

    function syncFirstMoveDeadlinesAfterMove(color, moveCommittedAt) {
        if (color === 'white' && !hasColorMadeMove('black')) {
            whiteFirstMoveDeadlineAt = null;
            blackFirstMoveDeadlineAt = moveCommittedAt + FIRST_MOVE_TIMEOUT_MS;
            return;
        }
        if (color === 'black') {
            blackFirstMoveDeadlineAt = null;
        }
    }

    function handleFirstMoveTimeout(color) {
        if (gameOver) return;

        drawOfferBy = null;
        clearFirstMoveDeadlines();
        gameOver = true;
        setGameConclusion(color === 'white' ? '0-1' : '1-0', '시간 초과');
        turnStartedAt = null;
        stopTimer();
        showResolvedGameOver();
        renderBoard();
    }

    function wrapCloneState() {
        var originalCloneState = window.cloneState;
        if (typeof originalCloneState !== 'function') return;

        window.cloneState = function() {
            var state = originalCloneState.apply(this, arguments);
            state.whiteFirstMoveDeadlineAt = whiteFirstMoveDeadlineAt;
            state.blackFirstMoveDeadlineAt = blackFirstMoveDeadlineAt;
            return state;
        };
    }

    function wrapRestoreState() {
        var originalRestoreState = window.restoreState;
        if (typeof originalRestoreState !== 'function') return;

        window.restoreState = function(state) {
            var result = originalRestoreState.apply(this, arguments);
            whiteFirstMoveDeadlineAt = state && typeof state.whiteFirstMoveDeadlineAt === 'number' ? state.whiteFirstMoveDeadlineAt : null;
            blackFirstMoveDeadlineAt = state && typeof state.blackFirstMoveDeadlineAt === 'number' ? state.blackFirstMoveDeadlineAt : null;
            updateFirstMoveTimerDisplay();
            return result;
        };
    }

    function wrapUpdateTimerDisplay() {
        var originalUpdateTimerDisplay = window.updateTimerDisplay;
        if (typeof originalUpdateTimerDisplay !== 'function') return;

        window.updateTimerDisplay = function() {
            originalUpdateTimerDisplay.apply(this, arguments);
            updateFirstMoveTimerDisplay();
        };
    }

    function wrapStartTimer() {
        var originalStartTimer = window.startTimer;
        if (typeof originalStartTimer !== 'function') return;

        window.startTimer = function() {
            if (!isFirstMoveTimerEnabled()) {
                clearFirstMoveDeadlines();
                updateTimerDisplay();
                return originalStartTimer.apply(this, arguments);
            }

            stopTimer();
            if (!shouldRunTimerForCurrentTurn()) return;

            updateTimerDisplay();
            if (typeof resetTimerTickAnchor === 'function') resetTimerTickAnchor(turnStartedAt || Date.now());

            timerInterval = setInterval(function() {
                if (gameOver) {
                    stopTimer();
                    return;
                }

                if (typeof isOnlineMode === 'function' && isOnlineMode() && typeof isLocalOnlineTurn === 'function' && !isLocalOnlineTurn()) {
                    if (typeof resetTimerTickAnchor === 'function') resetTimerTickAnchor();
                    return;
                }

                var firstMoveDeadline = getFirstMoveDeadline(currentTurn);
                if (firstMoveDeadline) {
                    if (typeof resetTimerTickAnchor === 'function') resetTimerTickAnchor();
                    if (Date.now() >= firstMoveDeadline) {
                        handleFirstMoveTimeout(currentTurn);
                        updateTimerDisplay();
                        if (typeof isOnlineMode === 'function' && isOnlineMode() && onlineState && !onlineState.applyingRemoteState) {
                            syncOnlineGameState('finish');
                        }
                        return;
                    }
                    updateTimerDisplay();
                    return;
                }

                if (gameSetting.unlimited) {
                    stopTimer();
                    updateTimerDisplay();
                    return;
                }

                var tickResult = typeof consumeActiveTurnTime === 'function' ? consumeActiveTurnTime() : null;
                if (!tickResult) {
                    updateTimerDisplay();
                    return;
                }

                if (tickResult.color === 'white') {
                    if (whiteTime <= 0) drawOfferBy = null;
                    if (whiteTime <= 0) {
                        whiteTime = 0;
                        gameOver = true;
                        setGameConclusion('0-1', '시간 초과');
                        turnStartedAt = null;
                        stopTimer();
                        showResolvedGameOver();
                        renderBoard();
                    }
                } else {
                    if (blackTime <= 0) {
                        blackTime = 0;
                        gameOver = true;
                        setGameConclusion('1-0', '시간 초과');
                        turnStartedAt = null;
                        stopTimer();
                        showResolvedGameOver();
                        renderBoard();
                    }
                }

                updateTimerDisplay();

                if (typeof isOnlineMode === 'function' && isOnlineMode() && onlineState && !onlineState.applyingRemoteState && typeof shouldSyncOnlineTick === 'function' && shouldSyncOnlineTick(tickResult)) {
                    syncOnlineGameState(gameOver ? 'finish' : 'tick');
                }
            }, typeof TIMER_TICK_MS === 'number' ? TIMER_TICK_MS : 100);
        };
    }

    function wrapExecuteMove() {
        var originalExecuteMove = window.executeMove;
        if (typeof originalExecuteMove !== 'function') return;

        window.executeMove = function(fromRow, fromCol, toRow, toCol, promotionPiece, options) {
            var piece = board[fromRow] ? board[fromRow][fromCol] : '';
            var color = pieceColor(piece);
            var hadFirstMoveDeadline = !!getFirstMoveDeadline(color);
            var moveCommittedAt = Date.now();

            var result = originalExecuteMove.apply(this, arguments);

            if (hadFirstMoveDeadline && !gameOver && hasColorMadeMove(color)) {
                syncFirstMoveDeadlinesAfterMove(color, moveCommittedAt);
            }

            updateTimerDisplay();
            return result;
        };
    }

    function wrapUndoMove() {
        var originalUndoMove = window.undoMove;
        if (typeof originalUndoMove !== 'function') return;

        window.undoMove = function() {
            var result = originalUndoMove.apply(this, arguments);
            updateTimerDisplay();
            if (!timerInterval && shouldRunTimerForCurrentTurn()) startTimer();
            return result;
        };
    }

    function wrapInitGame() {
        var originalInitGame = window.initGame;
        if (typeof originalInitGame !== 'function') return;

        window.initGame = function() {
            var result = originalInitGame.apply(this, arguments);
            clearFirstMoveDeadlines();
            syncFirstMoveDeadlinesToCurrentState();
            updateTimerDisplay();
            if (!timerInterval && shouldRunTimerForCurrentTurn()) startTimer();
            return result;
        };
    }

    function wrapApplyOnlineGameState() {
        var originalApplyOnlineGameState = window.applyOnlineGameState;
        if (typeof originalApplyOnlineGameState !== 'function') return;

        window.applyOnlineGameState = function() {
            var result = originalApplyOnlineGameState.apply(this, arguments);
            syncFirstMoveDeadlinesToCurrentState();
            updateTimerDisplay();
            if (!timerInterval && shouldRunTimerForCurrentTurn()) startTimer();
            return result;
        };
    }

    function rehydrateActiveGameTimer() {
        var gameScreen = document.getElementById('game-screen');
        if (!gameScreen || !gameScreen.classList.contains('active')) return;

        syncFirstMoveDeadlinesToCurrentState();
        stopTimer();
        updateTimerDisplay();
        if (shouldRunTimerForCurrentTurn()) startTimer();
    }

    wrapCloneState();
    wrapRestoreState();
    wrapUpdateTimerDisplay();
    wrapStartTimer();
    wrapExecuteMove();
    wrapUndoMove();
    wrapInitGame();
    wrapApplyOnlineGameState();

    window.addEventListener('DOMContentLoaded', function() {
        rehydrateActiveGameTimer();
    });
})();
