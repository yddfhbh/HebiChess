"""Lightweight invariants for the frozen Python/WASM parity inputs."""
from pathlib import Path
import tempfile
import unittest

from .write_wasm_parity_reference import (FROZEN_NETWORK_SHA256,
                                          FROZEN_POSITIONS_SHA256,
                                          canonical_text_sha256, fens,
                                          reference_samples)


class _DeterministicNetwork:
    def evaluate(self, fen: str) -> float:
        return float(sum(map(ord, fen)))


class WasmParityReferenceTest(unittest.TestCase):
    def test_tracked_corpus_is_the_frozen_hundred_fens(self):
        root = Path(__file__).resolve().parents[2]
        corpus = root / "tests" / "data" / "wasm-parity-100.fen"
        self.assertEqual(canonical_text_sha256(corpus), FROZEN_POSITIONS_SHA256)
        self.assertEqual(len(fens(corpus)), 100)

    def test_lf_and_crlf_corpora_have_identical_hashes_and_samples(self):
        lines = [f"position-{index}" for index in range(100)]
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            lf_corpus = directory / "positions-lf.fen"
            crlf_corpus = directory / "positions-crlf.fen"
            lf_corpus.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
            crlf_corpus.write_bytes(("\r\n".join(lines) + "\r\n").encode("utf-8"))

            self.assertEqual(canonical_text_sha256(lf_corpus),
                             canonical_text_sha256(crlf_corpus))
            network = _DeterministicNetwork()
            self.assertEqual(reference_samples(network, lf_corpus),
                             reference_samples(network, crlf_corpus))

    def test_frozen_network_identity_is_full_sha256(self):
        self.assertEqual(FROZEN_NETWORK_SHA256,
                         "4c815d54bc6c9fbfc27ebc19ea48ff3b23d845c338cda710aad14a65215a7826")


if __name__ == "__main__":
    unittest.main()
