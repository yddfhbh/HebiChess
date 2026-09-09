# HebiChess NNUE training

This is a first, deliberately simple end-to-end training pipeline. It keeps the existing HalfKP-v1 feature ABI (49,152 inputs), `256 -> 32 -> 32 -> 1` network, clamp-to-`[0, 1]` activation, side-to-move centipawn convention, and `.hebinnue` v1 format unchanged. It does not enable NNUE as the production default and does not implement incremental accumulators.

## Windows / PowerShell quick start

```powershell
py -3.13 -m venv .venv-nnue
.\.venv-nnue\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r training\nnue\requirements.txt
python -c "import torch; print(torch.cuda.is_available()); print(torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'no CUDA GPU')"
```

Place a PGN and an externally downloaded Stockfish-compatible UCI executable on your PC. Neither Stockfish source/binary nor an NNUE file is included in this repository. Then run:

```powershell
python -m training.nnue.extract_positions --pgn games.pgn --output positions.txt --count 100000
python -m training.nnue.label_stockfish --positions positions.txt --stockfish "C:\tools\stockfish\stockfish.exe" --output train.bin --nodes 10000 --threads 8 --hash 1024
python -m training.nnue.train --train train.bin --output runs\hebi-v1 --epochs 10 --batch-size 4096 --device cuda
python -m training.nnue.evaluate --data train.bin --checkpoint runs\hebi-v1\best.pt --device cuda
python -m training.nnue.export runs\hebi-v1\best.pt runs\hebi-v1\best.hebinnue
python -m training.nnue.parity --engine .\build-release\HebiChess --keep-network runs\hebi-v1\best.hebinnue
```

Use `--device auto` to select CUDA when available and otherwise CPU. Reduce `--batch-size` for a smaller VRAM budget. `train.py` writes `checkpoint.pt` after every epoch, writes `best.pt` when validation loss improves, produces `best.hebinnue`, and runs a Python-reference export smoke check. Resume an interrupted run with the same output directory and `--resume`.

## Extraction and teacher policy

`extract_positions.py` reads legal main-line PGN positions, skips plies 1--4, and excludes positions after checkmate or stalemate. A canonical position key contains placement, side to move, castling, and en-passant fields but drops halfmove/fullmove clocks, so feature-identical clock-only FEN variants are deduplicated without losing STM. Reservoir sampling targets 20% opening (plies 5--20), 60% middlegame, and 20% endgame (non-king material <= 12); if a PGN lacks one phase, remaining unique positions backfill the requested count. It writes `game_id<TAB>FEN`, preserving source-game identity.

`label_stockfish.py` calls the supplied UCI executable at fixed `--nodes` (default 10,000) for reproducibility across machines. Scores are converted to the position's side-to-move POV and clamped to `[-2000, +2000]` cp by default. Mate positions are excluded by default; `--mate-policy clamp` retains them as the signed clamp value and records signed mate distance metadata. The first model uses only centipawn regression and `SmoothL1Loss` (Huber): no WDL, result, or policy targets.

## Dataset format

`train.bin` is `HEBIDAT1` version 1. Its little-endian header identifies the feature ABI, fixed record size, and sample count. Every record contains:

- source game ID (`uint32`)
- clamped STM target cp (`int32`) and signed mate distance (`int16`)
- STM plus white/black feature counts (`uint8`)
- 32 padded `uint16` white feature indices and 32 black feature indices

The fixed 141-byte record avoids FEN parsing every epoch and stores sparse features, never a 49,152-float dense vector. The reader rejects wrong magic, versions/layouts, invalid feature indices, truncation, and trailing bytes. Training uses a deterministic source-game hash split: roughly 90% training and 10% held-out validation, never splitting a game across both sets.

`evaluate.py` reports held-out MAE, RMSE, median absolute error, Pearson score correlation, plus MAE buckets for roughly equal (<=100 cp), small advantage (100--500 cp), and large advantage (>500 cp). These are fit metrics, not playing-strength claims.

## Small smoke pipeline

Before a 100k run, execute exactly the same pipeline with a small count and cheap teacher search:

```powershell
python -m training.nnue.extract_positions --pgn games.pgn --output smoke-positions.txt --count 500
python -m training.nnue.label_stockfish --positions smoke-positions.txt --stockfish "C:\tools\stockfish\stockfish.exe" --output smoke.bin --nodes 1000 --threads 1 --hash 128
python -m training.nnue.train --train smoke.bin --output runs\smoke --epochs 1 --batch-size 128 --device auto
python -m training.nnue.export runs\smoke\best.pt runs\smoke\best.hebinnue
python -m training.nnue.parity --engine .\build-release\HebiChess --keep-network runs\smoke\best.hebinnue
```

PyTorch-dependent commands fail with an explicit installation message when PyTorch is absent. The sparse dataset reader and its tests need only the standard library.

## Existing ABI/parity tools

`features.py` is the HalfKP-v1 feature reference; `reference.py` is a package-free `.hebinnue` validation oracle. The existing parity command above checks Python/C++ features, binary rejection behavior, and raw inference with a `< 1e-4` tolerance.
