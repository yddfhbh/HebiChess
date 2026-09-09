from __future__ import annotations
import torch
from torch import nn
from .features import INPUT_DIM, both_features, parse_fen

ACCUMULATOR, HIDDEN1, HIDDEN2 = 256, 32, 32


class HebiNnueV1(nn.Module):
    """Float32 reference. Linear weights use PyTorch's output,input layout."""

    def __init__(self):
        super().__init__()
        self.transform = nn.EmbeddingBag(INPUT_DIM, ACCUMULATOR, mode="sum", include_last_offset=False)
        self.transform_bias = nn.Parameter(torch.zeros(ACCUMULATOR))
        self.hidden1 = nn.Linear(ACCUMULATOR * 2, HIDDEN1)
        self.hidden2 = nn.Linear(HIDDEN1, HIDDEN2)
        self.output = nn.Linear(HIDDEN2, 1)
        self.reset_parameters()

    def reset_parameters(self):
        """Keep the clipped-ReLU stack away from saturation at initialization.

        A normal EmbeddingBag starts with order-1 weights. Summing roughly a
        board's worth of active HalfKP rows would therefore push most
        accumulator values outside [0, 1], where the clipped activation has no
        useful gradient. These ranges keep every stage in its linear region
        while still giving the output layer enough scale to propagate a
        meaningful centipawn regression gradient into the hidden layers.
        """
        nn.init.uniform_(self.transform.weight, -0.001, 0.001)
        nn.init.uniform_(self.transform_bias, 0.24, 0.26)
        nn.init.uniform_(self.hidden1.weight, -0.02, 0.02)
        nn.init.uniform_(self.hidden1.bias, 0.04, 0.06)
        nn.init.uniform_(self.hidden2.weight, -0.04, 0.04)
        nn.init.uniform_(self.hidden2.bias, 0.19, 0.21)
        nn.init.uniform_(self.output.weight, -10.0, 10.0)
        nn.init.uniform_(self.output.bias, -1.0, 1.0)

    def forward_indices(self, white, black, side: str):
        white = torch.clamp(white, 0.0, 1.0)
        black = torch.clamp(black, 0.0, 1.0)
        x = torch.cat((white, black) if side == "w" else (black, white), dim=1)
        return self.output(torch.clamp(self.hidden2(torch.clamp(self.hidden1(x), 0.0, 1.0)), 0.0, 1.0)).squeeze()

    def forward_batch(self, white_indices, white_offsets, black_indices, black_offsets, sides):
        """Evaluate packed sparse HalfKP features; returns STM-centipawn scores."""
        white = self.transform(white_indices, white_offsets) + self.transform_bias
        black = self.transform(black_indices, black_offsets) + self.transform_bias
        white = torch.clamp(white, 0.0, 1.0)
        black = torch.clamp(black, 0.0, 1.0)
        black_to_move = sides.to(dtype=torch.bool).unsqueeze(1)
        x = torch.cat((torch.where(black_to_move, black, white),
                       torch.where(black_to_move, white, black)), dim=1)
        x = torch.clamp(self.hidden1(x), 0.0, 1.0)
        x = torch.clamp(self.hidden2(x), 0.0, 1.0)
        return self.output(x).squeeze(1)

    @torch.no_grad()
    def evaluate_fen(self, fen: str) -> float:
        white_ids, black_ids = both_features(fen)
        _, side = parse_fen(fen)
        device = self.transform.weight.device
        w = self.transform(torch.tensor(white_ids, device=device), torch.tensor([0], device=device)) + self.transform_bias
        b = self.transform(torch.tensor(black_ids, device=device), torch.tensor([0], device=device)) + self.transform_bias
        return float(self.forward_indices(w, b, side).cpu())


def deterministic_network(seed=20260909):
    torch.manual_seed(seed)
    model = HebiNnueV1().float()
    for parameter in model.parameters():
        parameter.data.uniform_(-0.01, 0.01)
    return model
