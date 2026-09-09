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

    def forward_indices(self, white, black, side: str):
        white = torch.clamp(white, 0.0, 1.0); black = torch.clamp(black, 0.0, 1.0)
        x = torch.cat((white, black) if side == "w" else (black, white))
        return self.output(torch.clamp(self.hidden2(torch.clamp(self.hidden1(x), 0.0, 1.0)), 0.0, 1.0)).squeeze()

    @torch.no_grad()
    def evaluate_fen(self, fen: str) -> float:
        white_ids, black_ids = both_features(fen); _, side = parse_fen(fen)
        device = self.transform.weight.device
        w = self.transform(torch.tensor(white_ids, device=device), torch.tensor([0], device=device)) + self.transform_bias
        b = self.transform(torch.tensor(black_ids, device=device), torch.tensor([0], device=device)) + self.transform_bias
        return float(self.forward_indices(w, b, side).cpu())

def deterministic_network(seed=20260909):
    torch.manual_seed(seed); model = HebiNnueV1().float()
    for parameter in model.parameters(): parameter.data.uniform_(-0.01, 0.01)
    return model
