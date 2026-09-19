"""Record failure controls. These tests never create a Vulkan device."""
import copy
import unittest
from run_agfx_copy import CONTROLS, FEATURES, ROOT, check_record, sha256, validation_messages


class Records(unittest.TestCase):
    def setUp(self):
        self.golden = (ROOT / "tests/parity/fixtures/agfx/copy_buffer_to_buffer.bin").read_bytes()
        self.record = {"schema_version": 1, "run_token": "fresh", "status": "pass", "host_source_sha256": {"source": "hash"},
                       "required": 1, "executed": 1, "passed": 1,
                       "cases": [{"case_id": "agfx.copy_buffer_to_buffer", "status": "pass", "bytes": 256,
                                  "expected_sha256": sha256(self.golden), "actual_sha256": sha256(self.golden), "completion_values": [1, 2, 3]}],
                       "controls": [{"case_id": name, "status": "pass"} for name in sorted(CONTROLS)],
                       "device": {"validation": True, "synchronization_validation": True, "api_version": 1 << 22 | 4 << 12, "enabled_features": sorted(FEATURES)},
                       "memory_flags": {"upload": 6, "readback": 14, "device": 1}}

    def check(self, record=None, actual=None):
        check_record(self.record if record is None else record, "fresh", {"source": "hash"}, self.golden if actual is None else actual, self.golden)

    def test_valid_record(self):
        self.check()

    def test_empty_skipped_duplicate_and_wrong_count(self):
        for field, value in [("cases", []), ("cases", self.record["cases"] * 2), ("executed", 0), ("required", 0), ("passed", True), ("status", "skip")]:
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                record = copy.deepcopy(self.record)
                record[field] = value
                self.check(record)
        self.record["cases"][0]["status"] = "skip"
        with self.assertRaises(ValueError): self.check()

    def test_missing_short_and_wrong_output(self):
        for actual in [b"", self.golden[:-1], bytes(256)]:
            with self.subTest(length=len(actual)), self.assertRaises(ValueError): self.check(actual=actual)

    def test_stale_identity_and_interruption(self):
        for field, value in [("run_token", "old"), ("host_source_sha256", {}), ("schema_version", 2), ("status", "interrupted")]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                record = copy.deepcopy(self.record)
                record[field] = value
                self.check(record)

    def test_required_controls_cannot_disappear(self):
        self.record["controls"].pop()
        with self.assertRaises(ValueError): self.check()

    def test_completion_and_hash_must_match(self):
        for field, value in [("completion_values", [1, 3, 2]), ("actual_sha256", "stale")]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                record = copy.deepcopy(self.record)
                record["cases"][0][field] = value
                self.check(record)

    def test_validation_features_and_memory_are_required(self):
        for section, field, value in [("device", "validation", False), ("device", "synchronization_validation", False), ("device", "enabled_features", []), ("device", "api_version", 1 << 22 | 3 << 12), ("memory_flags", "readback", 2)]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                record = copy.deepcopy(self.record)
                record[section][field] = value
                self.check(record)

    def test_only_exact_intentional_loader_notice_is_separated(self):
        notice = '''AGFX validation: Layer "VK_LAYER_NV_optimus" forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'.'''
        failures = ["AGFX validation: bad native access", "Validation Error: VUID-bad", "SYNC-HAZARD-WRITE_AFTER_READ", notice + " unexpected suffix"]
        diagnostics, notices = validation_messages("\n".join([notice] + failures))
        self.assertEqual(notices, [notice])
        self.assertEqual(diagnostics, failures)


if __name__ == "__main__":
    unittest.main()
