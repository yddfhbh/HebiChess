"""Report regression metrics for a checkpoint on held-out sparse NNUE samples."""
from __future__ import annotations
import argparse
import math
from pathlib import Path

from .dataset import Reader
from .train import batch_tensors, device_for, require_torch, split_indices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True); parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--batch-size", type=int, default=4096); parser.add_argument("--device", default="auto")
    args = parser.parse_args(); torch = require_torch()
    from .model import HebiNnueV1
    device = device_for(torch, args.device); checkpoint = torch.load(args.checkpoint, map_location=device, weights_only=False)
    model = HebiNnueV1().float().to(device); model.load_state_dict(checkpoint.get("model_state_dict", checkpoint)); model.eval()
    predicted, target = [], []
    with Reader(args.data) as reader, torch.no_grad():
        _, indices = split_indices(reader)
        for start in range(0, len(indices), args.batch_size):
            samples = [reader[i] for i in indices[start:start + args.batch_size]]
            w, wo, b, bo, sides, targets = batch_tensors(torch, samples, device)
            predicted.extend(model.forward_batch(w, wo, b, bo, sides).cpu().tolist()); target.extend(targets.cpu().tolist())
    errors = sorted(abs(a - b) for a, b in zip(predicted, target)); mae = sum(errors) / len(errors)
    rmse = math.sqrt(sum((a - b) ** 2 for a, b in zip(predicted, target)) / len(errors))
    mean_p, mean_t = sum(predicted)/len(errors), sum(target)/len(errors)
    denom = math.sqrt(sum((x-mean_p)**2 for x in predicted) * sum((x-mean_t)**2 for x in target))
    correlation = sum((x-mean_p)*(y-mean_t) for x, y in zip(predicted, target)) / denom if denom else float("nan")
    print(f"held-out samples={len(errors)} MAE cp={mae:.2f} RMSE cp={rmse:.2f} median absolute error cp={errors[len(errors)//2]:.2f} correlation={correlation:.4f}")
    for name, predicate in (("roughly equal", lambda x: abs(x) <= 100), ("small advantage", lambda x: 100 < abs(x) <= 500), ("large advantage", lambda x: abs(x) > 500)):
        bucket = [abs(a-b) for a, b in zip(predicted, target) if predicate(b)]
        print(f"{name}: n={len(bucket)} MAE cp={(sum(bucket)/len(bucket) if bucket else float('nan')):.2f}")


if __name__ == "__main__": main()
