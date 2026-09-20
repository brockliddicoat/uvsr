"""Corruption controls for Features state, artifacts and independent oracles."""
import json
import struct
import unittest

import numpy as np
from run_s2h_features import (arrows_oracle, artifact, cameras, check_record, check_state, expected_post,
                              input_bytes, inputs, seed, sha256)


class FeaturesEvidence(unittest.TestCase):
    def test_features_011_missing_arrow_rows_and_nan_background_rejected(self):
        image = np.full((600, 800, 4), 188, dtype=np.uint8)
        image[50, 40, :3] = 137
        for row in range(1, 8): image[50 + row * 20, 40, :3] = 0
        image[45, 20, :3] = [225, 137, 137]
        arrows_oracle(image)
        for y, x, color in [(70, 40, 188), (599, 799, 0), (45, 20, 188)]:
            bad = image.copy(); bad[y, x, :3] = color
            with self.assertRaises(ValueError): arrows_oracle(bad)

    def setUp(self):
        self.step = next(s for s in inputs() if s['name'] == 'gather-alpha-start')
        self.before = input_bytes(seed(True), self.step, [0]*4, cameras()[1])
        self.expected, self.floating = expected_post(self.before, self.step)

    def test_features_001_valid_state_retains_source_padding_and_inputs(self):
        check_state(self.expected, self.expected, self.floating, 'valid')
        self.assertEqual(self.expected[8:16], struct.pack('<2I', 0x12345678, 0x87654321))
        self.assertEqual(self.expected[80:], self.before[80:])
        self.assertEqual(struct.unpack_from('<2i', self.expected, 64), (52, 350))

    def test_features_002_padding_corruption_rejected(self):
        bad = bytearray(self.expected); bad[8] ^= 1
        with self.assertRaisesRegex(ValueError, 'unexpected state'):
            check_state(bad, self.expected, self.floating, 'padding')

    def test_features_003_immutable_matrix_corruption_rejected(self):
        bad = bytearray(self.expected); bad[112] ^= 1
        with self.assertRaisesRegex(ValueError, 'unexpected state'):
            check_state(bad, self.expected, self.floating, 'matrix')

    def test_features_004_slider_nan_and_wrong_endpoint_rejected(self):
        for value in (float('nan'), 0.5):
            bad = bytearray(self.expected); struct.pack_into('<f', bad, 28, value)
            with self.assertRaisesRegex(ValueError, 'state word7'):
                check_state(bad, self.expected, self.floating, 'slider')

    def test_features_005_wrong_stride_rejected(self):
        with self.assertRaisesRegex(ValueError, 'incomplete state'):
            check_state(self.expected[:-16], self.expected, self.floating, 'stride')

    def test_features_006_source_release_sentinel_retains_capture(self):
        sentinel = dict(self.step, name='gather-alpha-sentinel', mouse=[-100.9, 0, 0, 0])
        value, _ = expected_post(self.expected, sentinel)
        self.assertEqual(value[64:80], bytes(16), 'float -100.9 is not the exact -100 sentinel')
        exact, _ = expected_post(self.expected, dict(sentinel, mouse=[-100, 0, 0, 0]))
        self.assertEqual(exact[64:80], self.expected[64:80])
        released, _ = expected_post(value, dict(sentinel, name='gather-alpha-release', mouse=[900, 650, 0, 0]))
        self.assertEqual(released[64:80], bytes(16))
        self.assertEqual(released[:64], value[:64])
        self.assertEqual(released[80:], value[80:])

    def test_features_007_refreshed_hash_does_not_hide_state_corruption(self):
        bad = bytearray(self.expected); bad[0] ^= 1; bad = bytes(bad)
        row = dict(output='case.post', bytes=384, sha256=sha256(bad))
        data = artifact(row, 'case.post', 384, lambda _: bad)
        with self.assertRaisesRegex(ValueError, 'unexpected state'):
            check_state(data, self.expected, self.floating, 'state')

    def test_features_008_stale_truncated_and_renamed_artifacts_rejected(self):
        row = dict(output='case.post', bytes=384, sha256=sha256(self.expected))
        for record, raw in [(dict(row, output='other.post'), self.expected),
                            (row, self.expected[:-1]), (dict(row, sha256='0'*64), self.expected)]:
            with self.assertRaises(ValueError): artifact(record, 'case.post', 384, lambda _: raw)

    def test_features_009_zero_case_and_stale_host_runs_rejected(self):
        record = dict(schema_version=1, status='pass', run_token='token', required=0, executed=0, cases=[])
        with self.assertRaisesRegex(ValueError, 'incomplete case'):
            check_record(record, 'token', {}, {}, 0, lambda _: b'')
        record.update(required=54, executed=54, cases=[{}]*54, host_source_sha256={'old': 'source'}, shader={}, optimization_level=0)
        with self.assertRaisesRegex(ValueError, 'wrong source'):
            check_record(record, 'token', {}, {}, 0, lambda _: b'')

    def test_features_010_promoted_float_inputs_and_complete_cases(self):
        self.assertEqual(len(inputs()), 54)
        self.assertEqual(len({s['name'] for s in inputs()}), 54)
        fractional = next(s for s in inputs() if s['name'] == 'gather-radio-green')
        encoded = input_bytes(seed(False), fractional, [0]*4, cameras()[1])
        self.assertEqual(struct.unpack_from('<4f', encoded, 320), tuple(np.asarray(fractional['mouse'], dtype=np.float32)))
        self.assertEqual(len(encoded), 384)
        self.assertNotEqual(json.loads(json.dumps(float(np.float32(129.9)))), 129.9)


if __name__ == '__main__':
    unittest.main()
