"""Lightweight invariants for the frozen Python/WASM parity inputs."""
from pathlib import Path
import unittest

from .write_wasm_parity_reference import (FROZEN_NETWORK_SHA256,
                                          FROZEN_POSITIONS_SHA256, fens, sha256)


class WasmParityReferenceTest(unittest.TestCase):
    def test_tracked_corpus_is_the_frozen_hundred_fens(self):
        root = Path(__file__).resolve().parents[2]
        corpus = root / "tests" / "data" / "wasm-parity-100.fen"
        self.assertEqual(sha256(corpus), FROZEN_POSITIONS_SHA256)
        self.assertEqual(len(fens(corpus)), 100)

    def test_frozen_network_identity_is_full_sha256(self):
        self.assertEqual(FROZEN_NETWORK_SHA256,
                         "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826")


if __name__ == "__main__":
    unittest.main()
