"""Train HebiNnueV1 from a sparse .bin dataset (CUDA is selected automatically)."""
from __future__ import annotations
import argparse
from pathlib import Path
import random
import time

from .dataset import Reader, collate


def require_torch():
    try:
        import torch
        return torch
    except ImportError as error:
        raise SystemExit("PyTorch is required for training; install with: pip install torch") from error


def device_for(torch, requested: str):
    if requested == "auto": return torch.device("cuda" if torch.cuda.is_available() else "cpu")
    if requested == "cuda" and not torch.cuda.is_available():
        raise SystemExit("--device cuda requested but CUDA is unavailable; install a CUDA-enabled PyTorch build")
    return torch.device(requested)


def split_indices(reader):
    # A deterministic game-id split prevents neighbouring positions from one game leaking across sets.
    train, validation = [], []
    for index, sample in enumerate(reader):
        bucket = (sample.game_id * 2654435761) % 10
        (validation if bucket == 0 else train).append(index)
    if not validation or not train: raise ValueError("dataset needs at least two source-game ids for a 90/10 split")
    return train, validation


def batch_tensors(torch, samples, device):
    white, white_offsets, black, black_offsets, sides, targets = collate(samples)
    return (torch.tensor(white, dtype=torch.long, device=device), torch.tensor(white_offsets, dtype=torch.long, device=device),
            torch.tensor(black, dtype=torch.long, device=device), torch.tensor(black_offsets, dtype=torch.long, device=device),
            torch.tensor(sides, dtype=torch.long, device=device), torch.tensor(targets, dtype=torch.float32, device=device))


def mean_loss(torch, model, reader, indices, batch_size, device, criterion):
    model.eval(); total = 0.0
    with torch.no_grad():
        for start in range(0, len(indices), batch_size):
            samples = [reader[i] for i in indices[start:start + batch_size]]
            w, wo, b, bo, sides, targets = batch_tensors(torch, samples, device)
            total += float(criterion(model.forward_batch(w, wo, b, bo, sides), targets)) * len(samples)
    return total / len(indices)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--train", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--epochs", type=int, default=10); parser.add_argument("--batch-size", type=int, default=4096)
    parser.add_argument("--learning-rate", type=float, default=1e-3); parser.add_argument("--device", default="auto")
    parser.add_argument("--resume", action="store_true"); parser.add_argument("--seed", type=int, default=20260909)
    args = parser.parse_args()
    if args.epochs <= 0 or args.batch_size <= 0: parser.error("epochs and batch size must be positive")
    torch = require_torch()
    from .model import HebiNnueV1
    device = device_for(torch, args.device); args.output.mkdir(parents=True, exist_ok=True)
    print(f"device: {device}" + (f" ({torch.cuda.get_device_name(device)})" if device.type == "cuda" else ""))
    random.seed(args.seed); torch.manual_seed(args.seed)
    with Reader(args.train) as reader:
        train_indices, validation_indices = split_indices(reader)
        print(f"samples: train={len(train_indices)} validation={len(validation_indices)}")
        model = HebiNnueV1().float().to(device); optimizer = torch.optim.Adam(model.parameters(), lr=args.learning_rate)
        criterion = torch.nn.SmoothL1Loss(); start_epoch = 0; best = float("inf")
        checkpoint_path = args.output / "checkpoint.pt"
        if args.resume:
            if not checkpoint_path.exists(): raise SystemExit(f"--resume requested but {checkpoint_path} does not exist")
            checkpoint = torch.load(checkpoint_path, map_location=device, weights_only=False)
            model.load_state_dict(checkpoint["model_state_dict"]); optimizer.load_state_dict(checkpoint["optimizer_state_dict"])
            start_epoch, best = checkpoint["epoch"], checkpoint["best_validation_loss"]
            print(f"resumed after epoch {start_epoch}")
        started = time.monotonic()
        for epoch in range(start_epoch + 1, args.epochs + 1):
            epoch_start = time.monotonic(); model.train(); random.shuffle(train_indices); total = 0.0
            for start in range(0, len(train_indices), args.batch_size):
                samples = [reader[i] for i in train_indices[start:start + args.batch_size]]
                w, wo, b, bo, sides, targets = batch_tensors(torch, samples, device)
                optimizer.zero_grad(set_to_none=True); loss = criterion(model.forward_batch(w, wo, b, bo, sides), targets)
                loss.backward(); optimizer.step(); total += float(loss.detach()) * len(samples)
            train_loss = total / len(train_indices)
            validation_loss = mean_loss(torch, model, reader, validation_indices, args.batch_size, device, criterion)
            state = {"epoch": epoch, "model_state_dict": model.state_dict(), "optimizer_state_dict": optimizer.state_dict(),
                     "best_validation_loss": min(best, validation_loss), "feature_set": 1}
            torch.save(state, checkpoint_path)
            if validation_loss < best:
                best = validation_loss; torch.save(state, args.output / "best.pt")
            seconds = time.monotonic() - epoch_start
            print(f"epoch {epoch}/{args.epochs} train loss={train_loss:.6f} validation loss={validation_loss:.6f} "
                  f"lr={optimizer.param_groups[0]['lr']:.3g} samples/sec={len(train_indices)/seconds:.1f} elapsed={time.monotonic()-started:.1f}s")
    from .export import export, smoke_check
    checkpoint = torch.load(args.output / "best.pt", map_location="cpu", weights_only=False)
    model = HebiNnueV1().float(); model.load_state_dict(checkpoint["model_state_dict"]); export(model, args.output / "best.hebinnue"); smoke_check(model, args.output / "best.hebinnue")
    print(f"wrote {args.output / 'best.pt'} and {args.output / 'best.hebinnue'}")


if __name__ == "__main__": main()
