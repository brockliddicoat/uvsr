"""Reject incomplete source comparisons and preserve exact image/state failures."""
import copy
import unittest
from compare_s2h_features import complete, differences, validation
from run_s2h_features import inputs


class FeaturesComparison(unittest.TestCase):
    def test_features_compare_001_nonzero_complete_sequence_required(self):
        record = dict(status='pass', required=54, executed=54, optimization_level=3,
                      cases=[dict(case_id=f's2h.features.{s["name"]}.opt3', status='pass', step=s) for s in inputs()])
        complete(record, 3)
        for cases in ([], record['cases'][:-1], record['cases'][::-1], [record['cases'][0]]*54):
            with self.assertRaises(ValueError): complete(dict(record, cases=cases), 3)

    def test_features_compare_002_input_changes_rejected(self):
        record = dict(status='pass', required=54, executed=54, optimization_level=3,
                      cases=[dict(case_id=f's2h.features.{s["name"]}.opt3', status='pass', step=s) for s in inputs()])
        changed = copy.deepcopy(record); changed['cases'][0]['step']['time'] += 1
        with self.assertRaisesRegex(ValueError, 'changed deterministic'): complete(changed, 3)

    def test_features_compare_003_signed_channel_differences_and_alpha(self):
        value = differences(bytes([0, 10, 20, 255]), bytes([255, 10, 20, 254]), True)
        self.assertEqual(value['max_channel_error'], 255)
        self.assertEqual(value['differing_pixels'], 1)
        self.assertEqual(value['alpha_differences'], 1)
        self.assertFalse(value['equal'])

    def test_features_compare_004_single_code_error_remains_failure(self):
        self.assertTrue(differences(bytes([127, 0, 0, 255]), bytes([127, 0, 0, 255]), True)['equal'])
        self.assertFalse(differences(bytes([128, 0, 0, 255]), bytes([127, 0, 0, 255]), True)['equal'])
        with self.assertRaises(ValueError): differences(b'', b'', True)

    def test_features_compare_005_state_word_difference_is_not_image_tolerance(self):
        value = differences(bytes([1, 0, 0, 0]), bytes(4), False)
        self.assertEqual(value['first_difference'], dict(index=0, actual=1, expected=0))
        self.assertFalse(value['equal'])

    def test_features_compare_006_missing_or_dirty_validation_rejected(self):
        validation(dict(inserted=True, diagnostics=[]))
        for record in ({}, dict(inserted=False, diagnostics=[]), dict(inserted=True, diagnostics=['warning'])):
            with self.assertRaises(ValueError): validation(record)


if __name__ == '__main__':
    unittest.main()
