"""Corruption controls for Hello execution evidence and its color-space oracle."""
import copy
import unittest
import numpy as np
from run_s2h_hello import NAMES, check_record, close_codes, srgb, structural, cameras


class HelloEvidence(unittest.TestCase):
    def setUp(self):
        self.record = dict(schema_version=1, status='pass', run_token='fresh', required=6, executed=6,
                           host_source_sha256={'host': 'hash'}, shader={'shader': 'hash'}, optimization_level=0,
                           cases=[dict(case_id=f's2h.hello.{name}.opt0', output=f's2h.hello.{name}.opt0.rgba8',
                                       status='pass', root_sha256='wrong') for name in NAMES])

    def rejected(self, record, message):
        with self.assertRaisesRegex(ValueError, message):
            check_record(record, 'fresh', {'host': 'hash'}, {'shader': 'hash'}, 0,
                         lambda name: self.fail('invalid identity reached readback'))

    def test_stale_run_is_rejected(self):
        self.record['run_token'] = 'previous'
        self.rejected(self.record, 'stale')

    def test_zero_cases_do_not_pass(self):
        self.record.update(required=0, executed=0, cases=[])
        self.rejected(self.record, 'incomplete')

    def test_missing_case_is_rejected(self):
        self.record['cases'].pop()
        self.rejected(self.record, 'incomplete')

    def test_stale_host_is_rejected(self):
        self.record['host_source_sha256'] = {'host': 'old'}
        self.rejected(self.record, 'source identity')

    def test_wrong_camera_is_rejected_before_readback(self):
        self.rejected(copy.deepcopy(self.record), 'camera root')

    def test_unorm_cannot_substitute_for_srgb(self):
        close_codes(np.array([188]), srgb(np.array([.5])), 'midgray')
        with self.assertRaisesRegex(ValueError, 'midgray'):
            close_codes(np.array([128]), srgb(np.array([.5])), 'midgray')

    def test_transparent_screen_cannot_pass(self):
        image = np.zeros((600, 800, 4), dtype=np.uint8)
        with self.assertRaisesRegex(ValueError, 'nonopaque'):
            structural({'screen': image}, cameras())

    def test_nonfinite_oracle_cannot_pass(self):
        with self.assertRaisesRegex(ValueError, 'nonfinite'):
            close_codes(np.array([128]), np.array([float('nan')]), 'invalid reference')


if __name__ == '__main__':
    unittest.main()
