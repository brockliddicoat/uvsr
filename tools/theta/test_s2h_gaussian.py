"""Independent controls for the full Gaussian evidence gate."""
import struct
import unittest
import numpy as np
from run_s2h_gaussian import artifact, check_header, check_record, check_samples, inputs, sha256


class GaussianControls(unittest.TestCase):
    def test_gaussian_001_fixed_original_ply_input_and_alignment(self):
        steps, values = inputs()
        self.assertEqual(len(steps), 8)
        self.assertEqual(len(values[0]), 51520)
        self.assertEqual(values[0][:16], bytes(16))
        self.assertEqual(values[0][-8:], bytes(8))
        self.assertEqual(values[0][384:388], b'ply\n')

    def test_gaussian_002_four_field_header_and_every_other_word_immutable(self):
        expected = inputs()[1][0]
        post = bytearray(expected);post[:16] = struct.pack('<4I', 382, 62, 0, 200)
        check_header(post, expected)
        for offset in [0, 4, 8, 12, 16, 92*4, 384, 51519]:
            bad = bytearray(post);bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaisesRegex(ValueError, 'header or input mutation'):
                check_header(bad, expected)

    def test_gaussian_003_truncated_header_or_input_rejected(self):
        expected = inputs()[1][0]
        post = struct.pack('<4I', 382, 62, 0, 200)+expected[16:]
        for bad in [post[:-1], post+bytes(4), b'']:
            with self.assertRaises(ValueError):check_header(bad, expected)

    def test_gaussian_004_artifact_name_hash_and_size_are_bound(self):
        data = b'abcd';row = dict(output='case.input', bytes=4, sha256=sha256(data))
        self.assertEqual(artifact(row, 'case.input', 4, lambda _: data), data)
        for change, raw in [({'output':'other.input'},data),({'output':'../case.input'},data),
                            ({'sha256':'0'*64},data),({},data[:-1]),({'bytes':8},data)]:
            with self.assertRaises(ValueError):artifact(dict(row, **change), 'case.input', 4, lambda _: raw)

    @staticmethod
    def sample_fixture(alpha=127/255):
        image = np.zeros((600,800,4), np.uint8);image[...,3] = 255
        samples = np.zeros((8,8,8,4), np.float32);samples[...,3] = alpha
        depth = np.zeros((8,8,8), np.float32)
        return image, samples, depth

    def test_gaussian_005_only_two_legal_half_alpha_codes(self):
        for alpha in [127/255,128/255]:check_samples(*self.sample_fixture(alpha), 'clear')
        for alpha in [0,126/255,.5,129/255,1]:
            with self.subTest(alpha=alpha), self.assertRaises(ValueError):
                check_samples(*self.sample_fixture(alpha), 'invalid clear')

    def test_gaussian_006_per_sample_color_corruption_reaches_resolve_oracle(self):
        for channel in range(3):
            image, samples, depth = self.sample_fixture()
            samples[3,5,6,channel] = 1
            with self.subTest(channel=channel), self.assertRaisesRegex(ValueError, 'eight-sample resolve'):
                check_samples(image, samples, depth, 'wrong sample')

    def test_gaussian_007_depth_coverage_nan_and_shape_rejected(self):
        image, samples, depth = self.sample_fixture()
        for value in [float('nan'),-.1,1.1,.5]:
            bad = depth.copy();bad[1,2,3] = value
            with self.subTest(value=value), self.assertRaises(ValueError):check_samples(image,samples,bad,'depth')
        with self.assertRaisesRegex(ValueError, 'shape'):check_samples(image,samples[:7],depth,'shape')
        samples[2,1,6,0] = float('nan')
        with self.assertRaisesRegex(ValueError, 'invalid MSAA samples'):check_samples(image,samples,depth,'nan')

    def test_gaussian_008_resolve_averages_all_eight_samples(self):
        image, samples, depth = self.sample_fixture()
        samples[:,:,:4,:3] = 1
        image[:,:,:3] = 188  # sRGB encoding of four white and four black samples.
        check_samples(image,samples,depth,'half')
        image[66,88,0] = 200
        with self.assertRaisesRegex(ValueError, 'eight-sample resolve'):check_samples(image,samples,depth,'pixel')

    def test_gaussian_009_zero_cases_and_stale_identity_rejected_before_artifacts(self):
        record = dict(run_token='token',status='pass',optimization_level=0,
                      host_source_sha256={'current':'source'},shader={},required=0,executed=0)
        with self.assertRaisesRegex(ValueError,'missing/zero-case'):
            check_record(record,'token',{'current':'source'},{},0,lambda _:b'')
        record.update(required=40,executed=40)
        with self.assertRaisesRegex(ValueError,'stale native'):
            check_record(record,'token',{'old':'source'},{},0,lambda _:b'')
        with self.assertRaisesRegex(ValueError,'foreign native'):
            check_record(record,'other',{'current':'source'},{},0,lambda _:b'')

    def test_gaussian_010_missing_or_reordered_targets_rejected(self):
        record = dict(run_token='token',status='pass',optimization_level=0,
                      host_source_sha256={},shader={},required=40,executed=40,cases=[])
        for cases in [[],[{'case_id':'duplicate'}]*40]:
            with self.assertRaisesRegex(ValueError,'missing/reordered source targets'):
                check_record(dict(record,cases=cases),'token',{},{},0,lambda _:b'')


if __name__ == '__main__':
    unittest.main()
