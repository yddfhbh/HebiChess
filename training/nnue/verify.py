from __future__ import annotations
import argparse
from .export import export
from .model import deterministic_network
from .features import both_features

if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("fen"); parser.add_argument("--network"); args = parser.parse_args()
    model = deterministic_network()
    if args.network: export(model, args.network)
    print("white", *both_features(args.fen)[0]); print("black", *both_features(args.fen)[1]); print("score", model.evaluate_fen(args.fen))
