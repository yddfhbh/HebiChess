import tempfile
import unittest
from pathlib import Path

from training.nnue.dataset import HEADER, Reader, Sample, sample_from_fen, write


class DatasetTest(unittest.TestCase):
    def test_roundtrip(self):
        fen = "8/8/4k3/3p4/3P4/4K3/8/8 b - - 9 42"
        sample = sample_from_fen(fen, -123, game_id=7)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "data.bin"
            self.assertEqual(write(path, [sample]), 1)
            with Reader(path) as reader:
                self.assertEqual(len(reader), 1); self.assertEqual(reader[0], sample)

    def test_malformed_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad.bin"; path.write_bytes(b"bad")
            with self.assertRaisesRegex(ValueError, "truncated"):
                Reader(path)
            path.write_bytes(HEADER.pack(b"BADMAGIC", 1, 1, 0, 0, 0))
            with self.assertRaisesRegex(ValueError, "magic"):
                Reader(path)


if __name__ == "__main__": unittest.main()
