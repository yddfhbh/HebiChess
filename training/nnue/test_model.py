import unittest

import torch

from .features import both_features, parse_fen
from .model import deterministic_network


FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "r1bq1rk1/ppp2ppp/2np1n2/8/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 w - - 0 8",
    "r1bq1rk1/ppp2ppp/2np1n2/8/2B1P3/2N2N2/PPPP1PPP/R1BQ1RK1 b - - 0 8",
]


def batch_score(model, fen):
    white_ids, black_ids = both_features(fen)
    _, side = parse_fen(fen)
    device = model.transform.weight.device
    white = torch.tensor(white_ids, dtype=torch.long, device=device)
    black = torch.tensor(black_ids, dtype=torch.long, device=device)
    offsets = torch.tensor([0], dtype=torch.long, device=device)
    sides = torch.tensor([1 if side == "b" else 0], dtype=torch.long, device=device)
    return float(model.forward_batch(white, offsets, black, offsets, sides)[0].detach().cpu())


class SinglePositionInferenceTest(unittest.TestCase):
    def test_evaluate_fen_matches_batch_path_for_both_sides(self):
        model = deterministic_network().eval()
        for fen in FENS:
            with self.subTest(fen=fen):
                self.assertAlmostEqual(model.evaluate_fen(fen), batch_score(model, fen), places=5)


if __name__ == "__main__":
    unittest.main()
