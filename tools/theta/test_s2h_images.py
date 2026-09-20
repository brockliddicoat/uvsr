"""Original image and two-execution evidence controls. No Vulkan calls."""
import copy
import unittest

from run_s2h_fixtures import FEATURES, check_record, compare_image, sha256


class ImageRecords(unittest.TestCase):
    def setUp(self):
        self.actual = bytes([31, 63, 127, 255]) * (800 * 600)
        self.record = dict(schema_version=1, run_token="fresh", case_id="s2h.image.GatherTest.opt0", status="pass",
                           host_source_sha256={"source": "hash"}, shader={"payload": "hash"}, optimization_level=0,
                           entry_point="gather_cs", dispatch_groups=[100, 75, 1], workgroup_size=[8, 8, 1],
                           root_bytes=0, root_sha256=sha256(b""), completion_values=[1, 2, 3, 4, 5],
                           resolution=[800, 600], format="RGBA8_UNORM", output="GatherTest-opt0.rgba8",
                           bytes=len(self.actual), actual_sha256=sha256(self.actual), golden_agreement="not checked",
                           source_executions=2, first_execution=dict(output="GatherTest-opt0-execution1.rgba8",
                               bytes=len(self.actual), sha256=sha256(self.actual)),
                           device=dict(validation=True, synchronization_validation=True, api_version=1 << 22 | 4 << 12,
                                       loader_api_version=1 << 22 | 4 << 12, enabled_features=sorted(FEATURES)))

    def check(self, record=None, first=None):
        check_record(self.record if record is None else record, "fresh", {"source": "hash"}, {"payload": "hash"},
                     0, "GatherTest", self.actual, images=True, first=self.actual if first is None else first)

    def test_complete_two_execution_record(self):
        self.check()
        result = compare_image(self.actual, self.actual)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["different_channels"], 0)
        self.assertIsNone(result["first_mismatch"])

    def test_first_execution_is_required_and_verified(self):
        for key, value in [("source_executions", 1), ("first_execution", None),
                           ("completion_values", [1, 2, 3]), ("format", "RGBA32_FLOAT"),
                           ("case_id", "s2h.GatherTest.opt0"), ("golden_agreement", "pass")]:
            record = copy.deepcopy(self.record)
            record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(record)
        for first in [b"", self.actual[:-1], bytes([0]) + self.actual[1:]]:
            with self.subTest(length=len(first)), self.assertRaises(ValueError):
                self.check(first=first)

    def test_rgba_mutations_fail_without_rounding_tolerance(self):
        for channel in range(4):
            actual = bytearray(self.actual)
            position = (37 * 800 + 23) * 4 + channel
            actual[position] -= 1
            result = compare_image(actual, self.actual)
            with self.subTest(channel=channel):
                self.assertEqual(result["status"], "fail")
                self.assertEqual(result["different_channels"], 1)
                self.assertEqual(result["different_pixels"], 1)
                self.assertEqual(result["maximum_channel_error"], 1)
                self.assertEqual(result["pixels_above_one"], 0)
                self.assertEqual(result["first_mismatch"], dict(x=23, y=37, channel="RGBA"[channel],
                    actual=actual[position], expected=self.actual[position]))

    def test_missing_short_or_empty_images_do_not_pass(self):
        for actual in (b"", self.actual[:-1]):
            with self.subTest(length=len(actual)), self.assertRaises(ValueError):
                compare_image(actual, self.actual)
        result = compare_image(bytes(len(self.actual)), self.actual)
        self.assertEqual(result["status"], "fail")
        self.assertEqual(result["pixels_above_one"], 800 * 600)


if __name__ == "__main__":
    unittest.main()
