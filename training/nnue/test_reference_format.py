"""Regression coverage for the Phase 4 v1/v2/v3 Python NNUE reader."""
from __future__ import annotations

import math
import struct
import tempfile
import unittest
from array import array
from pathlib import Path

from .features import FEATURE_SET_V1, INPUT_DIM
from .format import (CLIPPED_RELU_0_1, ENDIAN_MARKER, MAGIC, RELU,
                     SCALAR_FLOAT32, V1_HEADER, V2_HEADER, V3_HEADER,
                     fnv1a, parameter_count)
from .reference import Network, load


FEN = "8/8/8/8/8/8/8/K6k w - - 0 1"
V1_DIMS = (32, 32)
FROZEN_DIMS = (128, 128)
ZERO_BLOCK = b"\0" * (1024 * 1024)
ZERO_CHECKSUMS = {}


class ReferenceFormatTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "network.hebinnue"

    def tearDown(self):
        self.directory.cleanup()

    @staticmethod
    def zero_checksum(count):
        if count in ZERO_CHECKSUMS:
            return ZERO_CHECKSUMS[count]
        checksum, remaining = 1469598103934665603, count * 4
        while remaining:
            block = ZERO_BLOCK[:min(len(ZERO_BLOCK), remaining)]
            checksum = fnv1a(block, checksum)
            remaining -= len(block)
        ZERO_CHECKSUMS[count] = checksum
        return checksum

    @staticmethod
    def header(version, hidden1, hidden2, count, *, scale=1.0,
               activation=CLIPPED_RELU_0_1, checksum):
        if version == 1:
            return V1_HEADER.pack(MAGIC, 1, FEATURE_SET_V1, INPUT_DIM, 256,
                                  hidden1, hidden2, SCALAR_FLOAT32,
                                  ENDIAN_MARKER, count, checksum)
        if version == 2:
            return V2_HEADER.pack(MAGIC, 2, FEATURE_SET_V1, INPUT_DIM, 256,
                                  hidden1, hidden2, 1, SCALAR_FLOAT32,
                                  ENDIAN_MARKER, scale, count, checksum)
        return V3_HEADER.pack(MAGIC, 3, FEATURE_SET_V1, INPUT_DIM, 256,
                              hidden1, hidden2, 1, SCALAR_FLOAT32,
                              ENDIAN_MARKER, activation, scale, count, checksum)

    def write_zero_network(self, version, hidden1, hidden2, **kwargs):
        count = parameter_count(INPUT_DIM, 256, hidden1, hidden2, 1)
        checksum = kwargs.pop("checksum", self.zero_checksum(count))
        header = self.header(version, hidden1, hidden2, count,
                             checksum=checksum, **kwargs)
        with self.path.open("wb") as output:
            output.write(header)
            remaining = count * 4
            while remaining:
                block = ZERO_BLOCK[:min(len(ZERO_BLOCK), remaining)]
                output.write(block)
                remaining -= len(block)
        return count, checksum

    def test_v1_load_and_evaluate_remains_compatible(self):
        self.write_zero_network(1, *V1_DIMS)
        network = load(self.path)
        self.assertEqual((network.format_version, network.hidden1_dim, network.hidden2_dim), (1, 32, 32))
        self.assertEqual(network.evaluate(FEN), 0.0)

    def test_v2_load_evaluate_and_output_scale(self):
        self.write_zero_network(2, *FROZEN_DIMS, scale=2.5)
        network = load(self.path)
        self.assertEqual(network.format_version, 2)
        self.assertEqual(network.final_hidden_activation, "clipped_relu")
        self.assertEqual(network.output_scale, 2.5)
        values = array("f", [0.0]) * parameter_count(INPUT_DIM, 256, *FROZEN_DIMS, 1)
        values[-1] = 2.0
        self.assertEqual(Network(values, hidden1=128, hidden2=128,
                                 output_scale=2.5).evaluate(FEN), 5.0)

    def test_v3_clipped_final_hidden_activation(self):
        self.write_zero_network(3, *FROZEN_DIMS, activation=CLIPPED_RELU_0_1)
        network = load(self.path)
        self.assertEqual(network.final_hidden_activation, "clipped_relu")
        values = array("f", [0.0]) * parameter_count(INPUT_DIM, 256, *FROZEN_DIMS, 1)
        hidden2_bias = INPUT_DIM * 256 + 256 + 128 * 2 * 256 + 128 + 128 * 128
        values[hidden2_bias], values[hidden2_bias + 128] = 2.0, 3.0
        self.assertEqual(Network(values, hidden1=128, hidden2=128, format_version=3,
                                 final_hidden_activation="clipped_relu").evaluate(FEN), 3.0)

    def test_v3_relu_final_hidden_activation(self):
        self.write_zero_network(3, *FROZEN_DIMS, activation=RELU)
        network = load(self.path)
        self.assertEqual(network.final_hidden_activation, "relu")
        values = array("f", [0.0]) * parameter_count(INPUT_DIM, 256, *FROZEN_DIMS, 1)
        hidden2_bias = INPUT_DIM * 256 + 256 + 128 * 2 * 256 + 128 + 128 * 128
        values[hidden2_bias], values[hidden2_bias + 128] = 2.0, 3.0
        self.assertEqual(Network(values, hidden1=128, hidden2=128, format_version=3,
                                 final_hidden_activation="relu").evaluate(FEN), 6.0)

    def test_unknown_activation_is_rejected(self):
        count = parameter_count(INPUT_DIM, 256, *FROZEN_DIMS, 1)
        checksum = self.zero_checksum(count)
        self.path.write_bytes(self.header(3, *FROZEN_DIMS, count,
                                          activation=99, checksum=checksum))
        with self.assertRaisesRegex(ValueError, "unsupported final hidden activation"):
            load(self.path)

    def test_bad_checksum_is_rejected(self):
        count = parameter_count(INPUT_DIM, 256, *FROZEN_DIMS, 1)
        checksum = self.zero_checksum(count)
        self.write_zero_network(2, *FROZEN_DIMS, checksum=checksum ^ 1)
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            load(self.path)

    def test_truncated_file_is_rejected(self):
        count = parameter_count(INPUT_DIM, 256, *FROZEN_DIMS, 1)
        self.path.write_bytes(self.header(3, *FROZEN_DIMS, count,
                                          checksum=self.zero_checksum(count))[:-1])
        with self.assertRaisesRegex(ValueError, "truncated"):
            load(self.path)

    def test_trailing_data_is_rejected(self):
        self.write_zero_network(1, *V1_DIMS)
        with self.path.open("ab") as output:
            output.write(b"x")
        with self.assertRaisesRegex(ValueError, "trailing data"):
            load(self.path)

    def test_unsupported_version_is_rejected(self):
        self.path.write_bytes(struct.pack("<8sI", MAGIC, 4))
        with self.assertRaisesRegex(ValueError, "unsupported format version"):
            load(self.path)

    def test_non_finite_score_is_rejected(self):
        count = parameter_count(INPUT_DIM, 256, *V1_DIMS, 1)
        values = array("f", [0.0]) * count
        values[-1] = math.nan
        network = Network(values)
        with self.assertRaisesRegex(ValueError, "non-finite network score"):
            network.evaluate(FEN)


if __name__ == "__main__":
    unittest.main()
