"""Export a HebiNnueV1 state dict to the little-endian .hebinnue v1 ABI."""
from __future__ import annotations
import argparse, hashlib, struct
import torch
from .features import FEATURE_SET_V1, INPUT_DIM
from .model import ACCUMULATOR, HIDDEN1, HIDDEN2, deterministic_network

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

if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("output"); parser.add_argument("--seed", type=int, default=20260909)
    args = parser.parse_args(); export(deterministic_network(args.seed), args.output)
