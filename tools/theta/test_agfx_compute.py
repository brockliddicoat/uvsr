"""Compute record controls. No GPU or native process is run."""
import copy
import unittest
from run_agfx_compute import CASES, CONTROLS, FEATURES, ROOT, check_record, expected_bytes, sha256


class ComputeRecords(unittest.TestCase):
    def setUp(self):
        self.golden = (ROOT / "tests/parity/fixtures/agfx/compute_multi_dispatch_buffer.bin").read_bytes()
        self.artifacts = [{"payload_sha256": "opt0"}, {"payload_sha256": "opt3"}]
        self.outputs = {(level,slot): expected_bytes(slot, self.golden) for level,slot in CASES}
        self.record = {"schema_version": 1, "run_token": "fresh", "status": "pass", "host_source_sha256": {"source": "hash"},
                       "shaders": self.artifacts, "required": 4, "executed": 4, "passed": 4,
                       "controls": [{"case_id": name, "status": "pass"} for name in sorted(CONTROLS)],
                       "device": {"validation": True, "synchronization_validation": True, "api_version": 1 << 22 | 4 << 12, "loader_api_version": 1 << 22 | 4 << 12, "enabled_features": sorted(FEATURES)}, "cases": []}
        for index,(level,slot) in enumerate(CASES):
            self.record["cases"].append({"case_id": f"agfx.compute_multi_dispatch_buffer.opt{level}.slot{slot}", "status": "pass",
                "optimization_level": level, "resource": slot, "passes": 4, "dispatch_groups": [1,1,1], "workgroup_size": [64,1,1], "root_bytes": 16,
                "bytes": 1024, "selected_bytes": 256, "unchanged_sentinel_bytes": 768, "actual_sha256": sha256(self.outputs[level,slot]), "golden_sha256": sha256(self.golden),
                "payload_sha256": f"opt{level}", "completion_value": 6 + 9*index + index//2, "readback_completion_value": 10+9*index + index//2})

    def check(self, record=None, outputs=None):
        check_record(self.record if record is None else record, "fresh", {"source": "hash"}, self.artifacts, self.outputs if outputs is None else outputs, self.golden)

    def test_valid(self): self.check()

    def test_empty_skipped_duplicate_and_interrupted(self):
        for key,value in [("cases", []), ("cases", [self.record["cases"][0]]*4), ("status", "interrupted"), ("executed", 0), ("required", 0), ("passed", 0)]:
            record = copy.deepcopy(self.record); record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)
        self.record["cases"][1]["status"] = "skip"
        with self.assertRaises(ValueError): self.check()

    def test_stale_host_shader_and_run(self):
        for key,value in [("run_token", "old"), ("host_source_sha256", {}), ("shaders", [])]:
            record = copy.deepcopy(self.record); record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)

    def test_every_selected_and_sentinel_byte_is_checked(self):
        for index in range(1024):
            outputs = self.outputs.copy(); mutated = bytearray(outputs[0,1]); mutated[index] ^= 1; outputs[0,1] = bytes(mutated)
            with self.subTest(byte=index), self.assertRaises(ValueError): self.check(outputs=outputs)

    def test_missing_short_or_wrong_slot_output(self):
        for value in [b"", self.outputs[0,1][:-1], self.outputs[0,3], bytes(1024)]:
            outputs = self.outputs.copy(); outputs[0,1] = value
            with self.subTest(length=len(value)), self.assertRaises(ValueError): self.check(outputs=outputs)

    def test_configuration_completion_and_digests(self):
        for key,value in [("resource", 0), ("passes", 3), ("root_bytes", 20), ("dispatch_groups", [2,1,1]), ("workgroup_size", [32,1,1]), ("selected_bytes", 0), ("unchanged_sentinel_bytes", 0), ("actual_sha256", "old"), ("payload_sha256", "old"), ("completion_value", 0)]:
            record = copy.deepcopy(self.record); record["cases"][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)

    def test_controls_and_validation_required(self):
        self.record["controls"].pop()
        with self.assertRaises(ValueError): self.check()
        self.setUp(); self.record["device"]["validation"] = False
        with self.assertRaises(ValueError): self.check()
        self.setUp(); self.record["device"]["enabled_features"] = []
        with self.assertRaises(ValueError): self.check()


if __name__ == "__main__": unittest.main()
