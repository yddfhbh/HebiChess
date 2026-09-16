# HebiChess NNUE training

## Frozen final-hidden ReLU candidate export (v3)

The final candidate is the explicitly tagged `49152 -> 256 -> 128 -> 128 -> 1`
ReLU checkpoint. Its approved SHA256 is
`f26908cf4c5a9fa73c0c09dcbcda2d2a18a57c2f8169945e8f5b20245424f410` and it
must export as `.hebinnue` **v3**. Legacy v1 and v2 meanings do not change.

v3 is little-endian with an exact 68-byte `<8s10If2Q>` header: `magic`,
`format_version=3`, feature ABI, input/accumulator/hidden1/hidden2/output
dimensions, `scalar_type=1`, `endian_marker=0x01020304`,
`final_hidden_activation` (`1=CLIPPED_RELU_0_1`, `2=RELU`), `output_scale`,
`parameter_count`, and payload FNV-1a-64 checksum. Its payload remains the
same raw float32 row-major tensor order as v2. Accumulator and hidden1 always
use `clamp(x, 0, 1)`; only v3 final hidden2 dispatches on this enum.

Auto-selection is strict: clipped 32/32 is v1; only the approved
metadata-less legacy clipped 128/128 checkpoint is v2; every explicit 128/128
activation (including clipped) is v3. Thus a new metadata-less 128/128
checkpoint cannot be mistaken for v2. Unknown versions/activation enums fail
closed, and top-level and `pilot_config` metadata must agree.

The sidecar includes format and feature ABI versions, dimensions, scalar type,
activation, output scale, parameter count, payload checksum, and source and
exported SHA256 values.

### Windows export and parity runbook

```powershell
$checkpoint = "runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.pt"
$network = "runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.hebinnue"
$expectedCheckpointSha256 = "f26908cf4c5a9fa73c0c09dcbcda2d2a18a57c2f8169945e8f5b20245424f410"
if ((Get-FileHash $checkpoint -Algorithm SHA256).Hash.ToLower() -ne $expectedCheckpointSha256) { throw "frozen checkpoint SHA256 mismatch" }

python -m training.nnue.export $checkpoint $network --format-version 3
if ($LASTEXITCODE -ne 0) { throw "v3 export failed" }
python -m training.nnue.export_parity --checkpoint $checkpoint --network $network --positions "tests\data\wasm-parity-100.fen" --expected-checkpoint-sha256 $expectedCheckpointSha256
if ($LASTEXITCODE -ne 0) { throw "tensor/Python parity failed" }
cmake --build build-release --config Release
if ($LASTEXITCODE -ne 0) { throw "native Release build failed" }
python -m training.nnue.native_parity --engine ".\build-release\HebiChess.exe" --network $network --positions "tests\data\wasm-parity-100.fen"
if ($LASTEXITCODE -ne 0) { throw "native parity failed" }
```

The sidecar and parity output report exported SHA256, payload checksum, tensor
bit identity, max/mean absolute cp difference, worst FEN, threshold, and
PASS/FAIL. Required threshold: `max_abs_cp_diff < 1e-3 cp`.

## Phase 4 depth-5 search-path diagnostic (Windows)

This is a diagnostic decomposition only: it does not modify the frozen network,
its `output_scale`, search settings, or model selection. It writes per-FEN HCE,
normal NNUE, and `NNUE_STYLE_OFF` records. The latter disables only the
post-objective root-style verification path through a native diagnostic switch;
the normal UCI default remains unchanged.

```powershell
python -m training.nnue.search_benchmark `
  --engine ".\build-release\HebiChess.exe" `
  --network "runs\full-phase4-h128-128-lr1e-4\best_balanced.hebinnue" `
  --positions "tests\data\nnue-search-benchmark.fen" `
  --depth 5 --repetitions 2 --style-off-diagnostic `
  --output "runs\phase4-search-diagnostic-depth5.json"
if ($LASTEXITCODE -ne 0) { throw "Phase 4 search diagnostic failed" }
```

Each record includes qsearch (`qdelta_prunes`, `see_calls`, `see_prunes`,
stand-pat cutoffs, searched captures, and max ply), root-style, and NNUE static
evaluation range/bucket counters. The JSON `index` is zero-based, so compare
indices 8, 11, and 12 (human-facing positions #09, #12, and #13) directly
across the three modes. `NNUE_STYLE_OFF` is diagnostic-only and must
not be used for a production option, calibration decision, or model selection.

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

## Fresh TWIC heldout preparation (manual inputs only)

The primary acceptance source is the same source family as the 500k REAL
dataset: `TWIC1658`, `TWIC1659`, `TWIC1660`, and `TWIC1661` are training only;
`TWIC1662` is the primary fresh-heldout target. Lichess Standard Rated
2026-08 is not an acceptance dataset and remains only a future robustness
candidate. No command below downloads a PGN.

First manually place these source files in the repository:

- `runs\500k\source-pgn\twic1658.pgn`
- `runs\500k\source-pgn\twic1659.pgn`
- `runs\500k\source-pgn\twic1660.pgn`
- `runs\500k\source-pgn\twic1661.pgn`
- `runs\fresh-heldout-twic1662\raw\twic1662.pgn`

Then use this PowerShell workflow. `build_game_manifest` records two stable
SHA-256 values: `move_fingerprint` is normalized mainline UCI moves only, while
`identity_fingerprint` additionally includes normalized `White`, `Black`,
`Date`, `Round`, and `Result`. `Event` and `Site` stay diagnostic-only because
their spelling is publisher-dependent. `prepare_fresh_heldout` removes
fresh-internal identity duplicates and every training `identity_fingerprint`
intersection before sampling. A move-only match with a different identity is
an ambiguous overlap: preparation emits its source/index, headers, normalized
identifying headers, and fingerprint, then refuses to create the heldout.
After manual review, only `--allow-ambiguous-move-overlap` permits it; that
choice and the overlap count are recorded in the preparation summary. A
fallback TWIC issue can use the same command by changing `--pgn`, `--source`,
and `--output-dir`.

```powershell
python -m training.nnue.build_game_manifest
if ($LASTEXITCODE -ne 0) { throw "training game manifest failed" }

python -m training.nnue.prepare_fresh_heldout `
  --training-manifest "runs\500k\training-game-manifest.jsonl" `
  --pgn "runs\fresh-heldout-twic1662\raw\twic1662.pgn" --source TWIC1662 `
  --output-dir "runs\fresh-heldout-twic1662" --count 50000 --seed 20260913 --per-game-cap 64
if ($LASTEXITCODE -ne 0) { throw "fresh heldout preparation failed" }

python -m training.nnue.label_fresh_heldout `
  --positions "runs\fresh-heldout-twic1662\positions.jsonl" `
  --stockfish "C:\tools\stockfish\stockfish.exe" `
  --output "runs\fresh-heldout-twic1662\fresh-heldout.bin" `
  --nodes 50000 --threads 8 --hash 1024
if ($LASTEXITCODE -ne 0) { throw "fresh heldout labeling failed" }

python -m training.nnue.evaluate_fresh_heldout `
  --data "runs\fresh-heldout-twic1662\fresh-heldout.bin" `
  --output "runs\fresh-heldout-twic1662\evaluation.json" --device cuda
if ($LASTEXITCODE -ne 0) { throw "fresh heldout evaluation failed" }

python -m training.nnue.write_full_run_provenance
if ($LASTEXITCODE -ne 0) { throw "full-run provenance sidecar failed" }
```

The fresh labeler is deliberately locked to Stockfish `nodes=50000`,
`threads=8`, `hash=1024`, STM centipawns, `±2000` clamp, feature ABI v1, and
mate exclusion. It writes a binary record-to-fingerprint manifest and a JSON
summary with record/game/mate counts and target distribution. The evaluator
does no 90/10 split and evaluates exactly `best_mae.pt` and `best_balanced.pt`
from `runs\full-phase4-h128-128-lr1e-4`, reconstructing hidden widths from
each checkpoint state dictionary. It produces MAE, RMSE, median absolute error,
the five target buckets, LargeRatio, correlation, sign agreement, and prediction
minimum/maximum in one `evaluation.json`.

## Phase 4 final 100k pilot decision and locked full-training recipe

Residual-quadratic auxiliary loss is **REJECTED**.  Under the locked Pareto
constraint (`MAE <= 184`, `LT100 <= 63`), its 0.25 and 0.5 weights did not
improve on the baseline trade-off, and weight 1.0 produced no qualifying
checkpoint.  Do not perform further objective or loss ablations.

The Phase 4 full-retrain candidate is locked to `256 -> 128 -> 128 -> 1`,
SmoothL1 `beta=1`, learning rate `1e-4`, output LR multiplier `1`, target scale
`1`, large-target weight `1`, and residual-MSE weight `0`.  It is the recipe
from `runs/pilot125-h128-128-lr1e-4`.

Use `--full-dataset` for the final run.  It intentionally ignores subset
manifests and uses all valid records in `runs/500k/train.bin`; the existing
deterministic game-id 90/10 split remains unchanged.  Before optimization, it
prints train/validation record counts, their unique game-ID counts, and
`crossing=0` (or aborts on leakage).

At every epoch, `metrics.jsonl` receives the existing complete validation
metrics.  `best_mae.pt` is the lowest-development-MAE checkpoint.  Once the run
ends, `best_balanced.pt` is selected exactly once from the epoch metrics:

1. `MAE <= BEST_MAE.MAE + 0.75cp` and `LT100 <= BEST_MAE.LT100 + 5cp`.
2. Maximize `LargeRatio`.
3. Minimize `700-1200` MAE, then RMSE, then overall MAE (then epoch for a stable tie).

Both checkpoint payloads and their JSON summaries carry the locked selection
reason and core validation metrics.  Candidate model-only checkpoints are kept
only while their MAE can still meet the final `BEST_MAE + 0.75cp` bound; an
epoch that fails the current bound can never qualify later, so pruning is safe.
This avoids unconditionally retaining all 125 optimizer checkpoints.

The deterministic development split is not an unbiased final acceptance set.
After the full candidate run, obtain games absent from the current 500k source,
label a fresh heldout with identical Stockfish conditions, and compare
`BEST_MAE` and `BEST_BALANCED` exactly once.  Fix one winner, then do Python
export/parity, native C++ evaluator/ABI work, fixed-FEN search, self-play, and
the NNUE ACCEPT/RETRAIN decision.  Do not retune loss, LR, architecture, or
this checkpoint-selection rule after seeing that fresh heldout.

The approved Windows command (do not run until full-training approval) is:

```powershell
python -m training.nnue.pilot `
  --full-dataset --train "runs\500k\train.bin" --output "runs\full-phase4-h128-128-lr1e-4" `
  --loss smoothl1 --beta 1 --epochs 125 --batch-size 4096 --learning-rate 1e-4 `
  --output-lr-multiplier 1 --target-scale 1 --large-target-threshold 700 --large-target-weight 1 `
  --residual-mse-weight 0 --hidden1 128 --hidden2 128 --seed 20260909 --device cuda --skip-export
if ($LASTEXITCODE -ne 0) { throw "full Phase 4 training failed" }
```

## Output-LR 125-epoch large-target weighting pilot

The following fixed pilot keeps the architecture, initialization, subset manifest, SmoothL1 beta, and output learning-rate multiplier unchanged. Only the training loss weight for `abs(target_cp) >= 700` varies. Validation metrics and best-checkpoint selection remain unweighted overall validation MAE; every epoch is retained in `metrics.jsonl`.

Run the three RTX 4060 variants from the repository root in PowerShell:

```powershell
$common = @(
  "-m", "training.nnue.pilot",
  "--train", "runs\500k\train.bin",
  "--loss", "smoothl1",
  "--beta", "1",
  "--epochs", "125",
  "--batch-size", "4096",
  "--learning-rate", "0.001",
  "--output-lr-multiplier", "30",
  "--large-target-threshold", "700",
  "--seed", "20260909",
  "--subset-count", "100000",
  "--subset-manifest", "runs\pilot-subset-indices.txt",
  "--device", "cuda"
)
foreach ($spec in @(@{ Weight = "2"; Name = "pilot125-output-lr30-large2" },
                    @{ Weight = "4"; Name = "pilot125-output-lr30-large4" },
                    @{ Weight = "8"; Name = "pilot125-output-lr30-large8" })) {
  python @common --large-target-weight $spec.Weight --output "runs\$($spec.Name)"
  if ($LASTEXITCODE -ne 0) { throw "pilot failed: $($spec.Name)" }
}
```

Compare the existing `pilot125-output-lr30` baseline and the three variants at BEST and FINAL:

```powershell
$runs = @("pilot125-output-lr30", "pilot125-output-lr30-large2", "pilot125-output-lr30-large4", "pilot125-output-lr30-large8")
$rows = foreach ($name in $runs) {
  $best = Get-Content "runs\$name\best-summary.json" -Raw | ConvertFrom-Json
  $final = Get-Content "runs\$name\final-summary.json" -Raw | ConvertFrom-Json
  foreach ($point in @(@{ Label = "BEST"; Data = $best; Metrics = $best.best_validation },
                       @{ Label = "FINAL"; Data = $final; Metrics = $final.final_validation })) {
    $m = $point.Metrics
    [pscustomobject]@{
      Run = $name; Point = $point.Label
      Epoch = if ($point.Label -eq "BEST") { $point.Data.best_epoch } else { $point.Data.final_epoch }
      OverallMAE = [math]::Round($m.mae, 2)
      LT100MAE = [math]::Round($m.buckets.lt100.mae, 2)
      MAE300to700 = [math]::Round($m.buckets.300to700.mae, 2)
      MAE700to1200 = [math]::Round($m.buckets.700to1200.mae, 2)
      MAE1200to2000 = [math]::Round($m.buckets.1200to2000.mae, 2)
      LargeRatio = [math]::Round($m.large_advantage.prediction_target_absolute_magnitude_ratio, 3)
      Correlation = [math]::Round($m.correlation, 4)
      SignAgreement = [math]::Round($m.sign_agreement, 4)
    }
  }
}
$rows | Format-Table -AutoSize
```

## Target-scale parameterization pilot (historical)

`pilot.py --target-scale S` trains against `target_cp / S`. Validation metrics remain raw centipawns, while SmoothL1 uses `beta / S`. Checkpoints record `target_scale`; `best.hebinnue` scales only output weight and bias back to the raw-cp ABI.

The target-scale comparison showed no meaningful improvement over baseline; do not run additional scale variants.

Run the three requested RTX 4060 variants in PowerShell from the repository root:

```powershell
$common = @(
  "-m", "training.nnue.pilot",
  "--train", "runs\500k\train.bin", "--subset-manifest", "runs\pilot-subset-indices.txt",
  "--loss", "smoothl1", "--beta", "1", "--epochs", "125", "--batch-size", "4096",
  "--learning-rate", "0.001", "--output-lr-multiplier", "1", "--large-target-weight", "1",
  "--large-target-threshold", "700", "--subset-count", "100000", "--seed", "20260909",
  "--device", "cuda"
)
foreach ($spec in @(
  @{ Scale = "10";  Name = "target-scale10"  },
  @{ Scale = "100"; Name = "target-scale100" },
  @{ Scale = "400"; Name = "target-scale400" }
)) {
  python @common --target-scale $spec.Scale --output "runs\$($spec.Name)"
  if ($LASTEXITCODE -ne 0) { throw "pilot failed: $($spec.Name)" }
}
```

Compare every epoch and print the Pareto frontier plus constrained top 20:

```powershell
$runs = @("pilot125-beta1", "target-scale10", "target-scale100", "target-scale400")
$rows = foreach ($name in $runs) {
  Get-Content "runs\$name\metrics.jsonl" | ForEach-Object {
    $r = $_ | ConvertFrom-Json; $v = $r.validation
    [pscustomobject]@{
      Name=$name; Epoch=$r.epoch; MAE=$v.mae; RMSE=$v.rmse; LT100=$v.buckets.lt100.mae
      '100-300'=$v.buckets.'100to300'.mae; '300-700'=$v.buckets.'300to700'.mae
      '700-1200'=$v.buckets.'700to1200'.mae; '1200-2000'=$v.buckets.'1200to2000'.mae
      LargeRatio=$v.large_advantage.prediction_target_absolute_magnitude_ratio
      Correlation=$v.correlation; SignAgreement=$v.sign_agreement
    }
  }
}
$frontier = $rows | Where-Object {
  $candidate = $_
  -not ($rows | Where-Object {
    $_ -ne $candidate -and $_.MAE -le $candidate.MAE -and $_.LT100 -le $candidate.LT100 -and
    $_.LargeRatio -ge $candidate.LargeRatio -and
    ($_.MAE -lt $candidate.MAE -or $_.LT100 -lt $candidate.LT100 -or $_.LargeRatio -gt $candidate.LargeRatio)
  })
}
"PARETO FRONTIER (all epochs)"; $frontier | Sort-Object MAE | Format-Table -AutoSize
"TOP 20: MAE <= 187.5 and LT100 <= 60"; $rows | Where-Object { $_.MAE -le 187.5 -and $_.LT100 -le 60 } |
  Sort-Object LargeRatio -Descending | Select-Object -First 20 | Format-Table -AutoSize
```

## Dense-head capacity ablation

The default remains `256 -> 32 -> 32 -> 1`. Experimental runs accept `--hidden1` and `--hidden2`; accumulator width, features, activation, initialization rules, and the `.hebinnue v1` production ABI remain unchanged. Non-default widths are training/validation-only and print `experimental architecture: .hebinnue v1 export skipped`.

From the repository root, run the 10k/1-epoch 64/64 smoke first. The three 125-epoch runs start only when the smoke succeeds:

```powershell
$common = @(
  "-m", "training.nnue.pilot",
  "--train", "runs\500k\train.bin",
  "--loss", "smoothl1", "--beta", "1",
  "--epochs", "125", "--batch-size", "4096", "--learning-rate", "0.001",
  "--output-lr-multiplier", "1", "--target-scale", "1", "--large-target-weight", "1",
  "--large-target-threshold", "700", "--seed", "20260909", "--device", "cuda",
  "--subset-count", "100000", "--subset-manifest", "runs\pilot-subset-indices.txt",
  "--skip-export"
)

python @common --epochs 1 --hidden1 64 --hidden2 64 --subset-count 10000 `
  --subset-manifest "runs\pilot-smoke-10k-indices.txt" --output "runs\pilot125-h64-64-smoke10k"
if ($LASTEXITCODE -ne 0) { throw "64/64 10k smoke failed; full runs were not started" }

foreach ($spec in @(
  @{ Hidden1 = 64;  Hidden2 = 64;  Name = "pilot125-h64-64" },
  @{ Hidden1 = 128; Hidden2 = 64;  Name = "pilot125-h128-64" },
  @{ Hidden1 = 128; Hidden2 = 128; Name = "pilot125-h128-128" }
)) {
  python @common --hidden1 $spec.Hidden1 --hidden2 $spec.Hidden2 --output "runs\$($spec.Name)"
  if ($LASTEXITCODE -ne 0) { throw "pilot failed: $($spec.Name)" }
}
```

Compare every epoch, including the existing `pilot125-beta1` baseline. The first table selects the minimum overall MAE; the second applies `MAE <= 187.5` and `LT100 <= 60`, then sorts by `LargeRatio` descending.

```powershell
$runs = @("pilot125-beta1", "pilot125-h64-64", "pilot125-h128-64", "pilot125-h128-128")
$rows = foreach ($name in $runs) {
  Get-Content "runs\$name\metrics.jsonl" | ForEach-Object {
    $r = $_ | ConvertFrom-Json; $v = $r.validation
    [pscustomobject]@{
      Name=$name; Epoch=$r.epoch; MAE=$v.mae; RMSE=$v.rmse; LT100=$v.buckets.lt100.mae
      '100-300'=$v.buckets.'100to300'.mae; '300-700'=$v.buckets.'300to700'.mae
      '700-1200'=$v.buckets.'700to1200'.mae; '1200-2000'=$v.buckets.'1200to2000'.mae
      LargeRatio=$v.large_advantage.prediction_target_absolute_magnitude_ratio
      Correlation=$v.correlation; SignAgreement=$v.sign_agreement
    }
  }
}
"A. Minimum overall MAE (all epochs)"
$rows | Sort-Object MAE | Select-Object -First 20 | Format-Table -AutoSize
"B. LargeRatio top 20 with MAE <= 187.5 and LT100 <= 60"
$rows | Where-Object { $_.MAE -le 187.5 -and $_.LT100 -le 60 } |
  Sort-Object LargeRatio -Descending | Select-Object -First 20 | Format-Table -AutoSize
```

## Final output-layer reachable-range diagnostic

`diagnose_activations.py` also reports the final linear layer's effective raw-cp weight statistics and its theoretical output range under `hidden2 in [0, 1]`. It reports validation prediction/target min, max, p01, p05, p50, p95, and p99, plus observed/theoretical endpoint ratios. This is diagnostic-only: it does not alter the model or train anything.

Run the confirmed checkpoints from the repository root in PowerShell:

```powershell
$common = @(
  "-m", "training.nnue.diagnose_activations",
  "--train", "runs\500k\train.bin",
  "--subset-manifest", "runs\pilot-subset-indices.txt",
  "--subset-count", "100000", "--seed", "20260909",
  "--batch-size", "4096", "--device", "cuda"
)
python @common --checkpoint "runs\pilot125-h128-128\best.pt" `
  --output "runs\pilot125-h128-128\final-output-range-diagnostic.json"
if ($LASTEXITCODE -ne 0) { throw "h128/128 diagnostic failed" }
python @common --checkpoint "runs\pilot125-beta1\best.pt" `
  --output "runs\pilot125-beta1\final-output-range-diagnostic.json"
if ($LASTEXITCODE -ne 0) { throw "beta1 diagnostic failed" }
```

For the optional output-LR x30 comparison, run the same command only after confirming its existing checkpoint path from the run metadata; no path is assumed here.

## Nonlinear post-hoc calibration diagnostic

`calibrate_output_power.py` performs a training-free diagnostic on the exact
100k pilot validation population (normally 10021 positions). It assigns
complete validation games to calibration/evaluation halves using a deterministic
uint32 game-id hash, fits `sign(pred) * a * abs(pred)^gamma` for overall MAE on
the calibration half only, and reports raw/calibrated metrics on the held-out
evaluation half. It does not modify or save the checkpoint.

Run from the repository root in Windows PowerShell:

```powershell
python -m training.nnue.calibrate_output_power `
  --train "runs\500k\train.bin" `
  --checkpoint "runs\pilot125-h128-128\best.pt" `
  --subset-manifest "runs\pilot-subset-indices.txt" `
  --subset-count 100000 --seed 20260909 `
  --batch-size 4096 --device cuda
if ($LASTEXITCODE -ne 0) { throw "nonlinear calibration diagnostic failed" }
```

Use the printed `CALIBRATED prediction` row to apply the stated decision rule:
large-bucket improvement with stable `MAE`/`LT100` supports an output-calibration
problem; simultaneous large-bucket gain and strong small/overall degradation
points toward a representation/features limitation.

## Non-parametric magnitude calibration/discrimination diagnostic

`diagnose_magnitude_calibration.py` reuses the same pilot validation population
and deterministic game-id 50/50 calibration/evaluation split. It reports
Spearman/Kendall correlation, target-magnitude bucket distributions,
rank-balanced predicted-magnitude deciles, and a training-free PAVA isotonic
mapping fitted on the calibration half. The evaluation table compares RAW and
ISOTONIC; no checkpoint or prediction file is written.

Run from the repository root in Windows PowerShell:

```powershell
python -m training.nnue.diagnose_magnitude_calibration `
  --train "runs\500k\train.bin" `
  --checkpoint "runs\pilot125-h128-128\best.pt" `
  --subset-manifest "runs\pilot-subset-indices.txt" `
  --subset-count 100000 --seed 20260909 `
  --batch-size 4096 --device cuda
if ($LASTEXITCODE -ne 0) { throw "magnitude calibration diagnostic failed" }
```

Interpret isotonic against POWER from the preceding diagnostic: a clear
large-bucket gain with stable `MAE`/`LT100` indicates recoverable output
calibration; a similar `LargeRatio` ceiling and persistent trade-offs indicate
limited raw magnitude discrimination.

## Two-stage output-head Ridge refit

`refit_output_head.py` is the final pristine two-stage diagnostic. It freezes
the selected pilot checkpoint. Every `game_id` occurring in the 100k pilot
manifest is excluded from the final test; only records from every other full
dataset game form `PRISTINE`. The script prints the total, pilot, and pristine
game counts plus the pristine record count before the diagnostic results.

The already-inspected pilot validation positions are used only for deterministic
game-grouped 5-fold cross-validation. For every fold, Ridge is fit on four
folds using clipped `hidden2`; the held-out fold receives predictions for all
lambda/alpha candidates. The combined out-of-fold predictions select the
recipe under `MAE <= ORIGINAL OOF + 2cp` and `LT100 <= 63`, maximizing
`LargeRatio`, then minimizing `700-1200` MAE and overall MAE. After selection,
Ridge is fit once on all pilot validation positions. `PRISTINE` is never used
to select `lambda` or `alpha`.

The final output reports OOF selection metrics and pristine `ORIGINAL`,
`RIDGE_FULL`, and `RIDGE_BLEND_SELECTED` metrics, including prediction min/max.
The pristine alpha curve is explicitly `diagnostic_only=true; do_not_reselect=true`.
The checkpoint is never overwritten and this diagnostic does not save a head.

Run from the repository root in Windows PowerShell:

```powershell
python -m training.nnue.refit_output_head `
  --train "runs\500k\train.bin" `
  --checkpoint "runs\pilot125-h128-128-lr1e-4\best.pt" `
  --subset-manifest "runs\pilot-subset-indices.txt" `
  --subset-count 100000 --seed 20260909 `
  --batch-size 4096 --folds 5 --device cuda
if ($LASTEXITCODE -ne 0) { throw "output-head Ridge refit failed" }
```

If no OOF candidate is feasible, the script reports a minimum-MAE fallback and
the final pristine verdict is `REJECTED`. Otherwise `VALID` requires pristine
`MAE <= ORIGINAL + 2`, `LT100 <= 63`, and `LargeRatio >= 0.20`. `STRONG`
requires `MAE <= ORIGINAL + 1`, `LT100 <= 62`, `LargeRatio >= 0.21`, and
correlation no worse than `ORIGINAL`.

## Counterfactual final-hidden activation diagnostic (Windows)

`diagnose_final_hidden_counterfactual.py` manually forwards only the frozen
full checkpoint's existing game-id-disjoint validation split. It holds every
weight, accumulator transform, hidden1 transform, output layer, dataset, and
split fixed. It changes only the inference-time transform applied to final
`hidden2_pre`, writing one new JSON report. It does not train, export, use
TWIC1600, run a search benchmark, or modify a checkpoint.
Before inference it verifies the fixed Phase 4 dataset and frozen checkpoint
SHA-256 values, so a different input cannot be substituted accidentally.

Synchronize this file to the Windows workspace:

- `training\\nnue\\diagnose_final_hidden_counterfactual.py`

From the repository root in Windows PowerShell, run exactly this diagnostic:

```powershell
python -m training.nnue.diagnose_final_hidden_counterfactual `
  --train "runs\\500k\\train.bin" `
  --checkpoint "runs\\full-phase4-h128-128-lr1e-4\\best_balanced.pt" `
  --output "runs\\full-phase4-h128-128-lr1e-4\\final-hidden-counterfactual.json" `
  --batch-size 4096 --device cuda
if ($LASTEXITCODE -ne 0) { throw "final-hidden counterfactual diagnostic failed" }
```

The JSON reports global MAE/RMSE/correlation/sign accuracy and prediction
range; the five fixed target-magnitude buckets; and `>=700` / `>=1200` tails.
Each counterfactual contains deltas versus baseline `clamp(pre_h2, 0, 1)`.
Tail `magnitude_slope` is ordinary least-squares slope of `abs(prediction)` on
`abs(target)`, with an intercept. `identity_diagnostic_only` is specifically
for estimating lower-clamp information loss and is not an architecture
recommendation. A counterfactual win does not make it a trained model or a
production bound; select any retraining pilot only after inspecting this
report. If justified, prefer the smallest comparison: current `[0,1]` control
against final-hidden ReLU `[0,+inf)`, or one narrowly justified bounded cap.

## Final-hidden ReLU full experiment (Windows, training only)

This is the one approved architecture experiment following the frozen
counterfactual diagnosis.  It changes exactly one trained-model operation:
final `hidden2` post-activation from `clamp(pre_h2, 0, 1)` to
`max(pre_h2, 0)`.  Accumulator and hidden1 activations remain clipped ReLU;
feature ABI, widths, loss, learning rate, target representation, weighting,
residual MSE, dataset, deterministic game-id split, and seed remain the locked
Phase 4 recipe.  `--final-hidden-activation` defaults to `clipped_relu`, so
existing training behavior and legacy checkpoints remain unchanged.

Synchronize these files to the Windows workspace before the run:

- `training\nnue\model.py`
- `training\nnue\pilot.py`
- `training\nnue\diagnose_final_hidden_counterfactual.py`
- `training\nnue\validate_final_hidden_relu.py`
- `training\nnue\README.md`

From the repository root in Windows PowerShell, run the following.  It is a
full 500k run, not a 100k pilot, and does not export `.hebinnue`:

```powershell
$expectedDatasetSha256 = "de034e7892c703682341b9fda402b78bddf0ecd65ddf53a25b76a68212d33825"
$actualDatasetSha256 = (Get-FileHash "runs\500k\train.bin" -Algorithm SHA256).Hash.ToLower()
if ($actualDatasetSha256 -ne $expectedDatasetSha256) { throw "unexpected Phase 4 dataset SHA-256: $actualDatasetSha256" }

python -m training.nnue.pilot `
  --full-dataset --train "runs\500k\train.bin" --output "runs\full-phase4-finalrelu-h128-128-lr1e-4" `
  --loss smoothl1 --beta 1 --epochs 125 --batch-size 4096 --learning-rate 1e-4 `
  --output-lr-multiplier 1 --target-scale 1 --large-target-threshold 700 --large-target-weight 1 `
  --residual-mse-weight 0 --hidden1 128 --hidden2 128 --final-hidden-activation relu `
  --seed 20260909 --device cuda --skip-export
if ($LASTEXITCODE -ne 0) { throw "final-hidden ReLU full training failed" }
```

The normal full-run artifacts are retained in the new directory:
`checkpoint.pt`, `final.pt`, `metrics.jsonl`, `best_mae.pt`,
`best_balanced.pt`, and their summaries.  Every checkpoint and summary records
`final_hidden_activation`, preventing a ReLU checkpoint from being mistaken for
the default clipped-ReLU architecture.  Do not export, run native parity or
search tests, use TWIC1600, or make a production decision at this stage.

After training, compare only the trained ReLU `BEST_BALANCED` checkpoint with
the frozen original `BEST_BALANCED` checkpoint on their common original 90/10
game-id split:

```powershell
python -m training.nnue.validate_final_hidden_relu `
  --train "runs\500k\train.bin" `
  --baseline "runs\full-phase4-h128-128-lr1e-4\best_balanced.pt" `
  --candidate "runs\full-phase4-finalrelu-h128-128-lr1e-4\best_balanced.pt" `
  --output "runs\full-phase4-finalrelu-h128-128-lr1e-4\post-train-validation.json" `
  --batch-size 4096 --device cuda
if ($LASTEXITCODE -ne 0) { throw "final-hidden ReLU post-training validation failed" }
```

The JSON contains the requested global metrics (MAE, RMSE, correlation, sign
accuracy, prediction min/max); each fixed target bucket's MAE, RMSE, mean
`|P|`, `|P|/|T|`, correlation, and sign accuracy; `>=700`/`>=1200` tail mean
and median `|P|`, ratio, and magnitude slope; and ReLU final-hidden2 post-
activation zero/positive fractions, p50/p90/p95/p99, and maximum.  It includes
no upper-saturation metric because ReLU has no upper clamp.

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
