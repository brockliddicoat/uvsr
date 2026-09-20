"""Controls for fixture evidence. These tests never run Vulkan."""
import copy
import struct
import unittest

from run_s2h_fixtures import FEATURES, camera_root, check_record, sha256


class FixtureRecords(unittest.TestCase):
    def setUp(self):
        self.actual = struct.pack("<4f", 0.25, 0.5, 0.75, 1.0) * (800 * 600)
        root = camera_root()
        self.record = dict(schema_version=1, run_token="fresh", case_id="s2h.3DTest.opt0", status="pass",
                           host_source_sha256={"source": "hash"}, shader={"payload": "hash"}, optimization_level=0,
                           entry_point="world_cs", dispatch_groups=[100, 75, 1], workgroup_size=[8, 8, 1],
                           root_bytes=80, root_sha256=sha256(root), completion_values=[1, 2, 3],
                           resolution=[800, 600], format="RGBA32_FLOAT", output="3DTest-opt0.rgba32f",
                           bytes=len(self.actual), actual_sha256=sha256(self.actual), golden_agreement="not checked",
                           device=dict(validation=True, synchronization_validation=True, api_version=1 << 22 | 4 << 12,
                                       loader_api_version=1 << 22 | 4 << 12, enabled_features=sorted(FEATURES)))

    def check(self, record=None, actual=None):
        check_record(self.record if record is None else record, "fresh", {"source": "hash"}, {"payload": "hash"},
                     0, "3DTest", self.actual if actual is None else actual)

    def test_valid_complete_finite_readback(self):
        self.check()

    def test_stale_or_skipped_evidence(self):
        for key, value in [("run_token", "old"), ("case_id", "s2h.2DTest.opt0"), ("status", "skip"),
                           ("host_source_sha256", {}), ("shader", {}), ("actual_sha256", "old")]:
            record = copy.deepcopy(self.record)
            record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(record)

    def test_root_dispatch_completion_and_pixel_contracts(self):
        for key, value in [("root_bytes", 0), ("root_sha256", sha256(bytes(80))), ("dispatch_groups", [0, 75, 1]),
                           ("workgroup_size", [1, 1, 1]), ("completion_values", [1, 1, 1]), ("entry_point", "gather_cs"),
                           ("resolution", [600, 800]), ("format", "RGBA8_UNORM"), ("output", "old.rgba32f")]:
            record = copy.deepcopy(self.record)
            record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(record)

    def test_missing_short_mutated_or_empty_readback(self):
        for value in [b"", self.actual[:-1], bytes(len(self.actual)), b"x" + self.actual[1:]]:
            with self.subTest(length=len(value)), self.assertRaises(ValueError):
                self.check(actual=value)

    def test_nonfinite_readback_rejected_even_with_matching_hash(self):
        for value in [float("nan"), float("inf"), -float("inf")]:
            actual = struct.pack("<f", value) + self.actual[4:]
            self.record["actual_sha256"] = sha256(actual)
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.check(actual=actual)

    def test_negative_zero_is_still_empty(self):
        actual = struct.pack("<f", -0.0) * (800 * 600 * 4)
        self.record["actual_sha256"] = sha256(actual)
        with self.assertRaises(ValueError):
            self.check(actual=actual)

    def test_validation_and_features_required(self):
        for key, value in [("validation", False), ("synchronization_validation", False), ("enabled_features", [])]:
            record = copy.deepcopy(self.record)
            record["device"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(record)

    def test_float_execution_cannot_claim_original_png_parity(self):
        self.record["golden_agreement"] = "pass"
        with self.assertRaises(ValueError):
            self.check()


if __name__ == "__main__":
    unittest.main()
