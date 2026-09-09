"""Export a HebiNnueV1 state dict to the little-endian .hebinnue v1 ABI."""
from __future__ import annotations
import argparse, struct
from pathlib import Path
from .features import FEATURE_SET_V1, INPUT_DIM

ACCUMULATOR, HIDDEN1, HIDDEN2 = 256, 32, 32

HEADER = struct.Struct("<8s8I2Q")
PARAMETERS = INPUT_DIM*ACCUMULATOR + ACCUMULATOR + HIDDEN1*(2*ACCUMULATOR) + HIDDEN1 + HIDDEN2*HIDDEN1 + HIDDEN2 + HIDDEN2 + 1

def fnv1a(data):
    value = 1469598103934665603
    for byte in data: value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value

def export(model, path):
    state = model.state_dict()
    tensors = [state["transform.weight"], state["transform_bias"], state["hidden1.weight"], state["hidden1.bias"], state["hidden2.weight"], state["hidden2.bias"], state["output.weight"], state["output.bias"]]
    payload = b"".join(t.detach().cpu().contiguous().float().numpy().astype("<f4", copy=False).tobytes() for t in tensors)
    if len(payload) != PARAMETERS * 4: raise ValueError("unexpected parameter count")
    header = HEADER.pack(b"HEBINNUE", 1, FEATURE_SET_V1, INPUT_DIM, ACCUMULATOR, HIDDEN1, HIDDEN2, 1, 0x01020304, PARAMETERS, fnv1a(payload))
    with open(path, "wb") as output: output.write(header); output.write(payload)

def smoke_check(model, path):
    """Compare a just-exported file with PyTorch on a fixed legal position."""
    from .reference import load
    network = load(path)
    smoke_fen = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"
    expected, actual = model.evaluate_fen(smoke_fen), network.evaluate(smoke_fen)
    if abs(expected - actual) >= 1e-4:
        raise RuntimeError(f"export smoke mismatch: torch={expected} reference={actual}")
    print(f"export smoke: PASS (abs diff {abs(expected - actual):.8f})")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", nargs="?", type=Path, help="training checkpoint (.pt)")
    parser.add_argument("output", type=Path)
    parser.add_argument("--seed", type=int, help="create a deterministic validation network instead")
    args = parser.parse_args()
    try:
        import torch
        from .model import HebiNnueV1, deterministic_network
    except ImportError as error:
        raise SystemExit("PyTorch is required for export; install with: pip install torch") from error
    if args.checkpoint is None:
        if args.seed is None:
            parser.error("checkpoint is required (or pass --seed for a validation network)")
        model = deterministic_network(args.seed)
    else:
        if args.seed is not None:
            parser.error("--seed cannot be combined with a checkpoint")
        checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=False)
        state = checkpoint.get("model_state_dict", checkpoint) if isinstance(checkpoint, dict) else checkpoint
        model = HebiNnueV1().float()
        model.load_state_dict(state)
    export(model, args.output)
    smoke_check(model, args.output)
