"""Texture evidence controls. No native process or GPU is used."""
import copy
import unittest
from run_agfx_textures import COMPLETIONS, CONTROLS, check_record, expected_outputs, sha256


class TextureRecords(unittest.TestCase):
    def setUp(self):
        self.outputs = expected_outputs()
        self.record = dict(schema_version=1, run_token="fresh", status="pass", required=6, executed=6, passed=6,
                           host_source_sha256={"source": "hash"},
                           device=dict(validation=True, synchronization_validation=True,
                                       api_version=1 << 22 | 4 << 12, loader_api_version=1 << 22 | 4 << 12),
                           controls=[dict(case_id=name, status="pass") for name in sorted(CONTROLS)], cases=[])
        for (name, data), completed in zip(self.outputs.items(), COMPLETIONS):
            self.record["cases"].append(dict(case_id=name, status="pass", bytes=len(data),
                actual_sha256=sha256(data), expected_sha256=sha256(data), completion_values=completed))

    def check(self, record=None, outputs=None):
        check_record(self.record if record is None else record, "fresh", {"source": "hash"}, self.outputs if outputs is None else outputs)

    def test_valid_source_goldens_and_controls(self):
        self.check()

    def test_missing_skipped_duplicated_and_unknown_execution(self):
        for key, value in [("cases", []), ("cases", [self.record["cases"][0]] * 6), ("status", "skip"),
                           ("required", 0), ("executed", None), ("passed", 0), ("run_token", "old"), ("host_source_sha256", {})]:
            record = copy.deepcopy(self.record)
            record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(record)

    def test_pixels_alpha_prefix_padding_and_float_bytes_are_checked(self):
        for name, data in self.outputs.items():
            for offset in [0, 3, 11, len(data) // 2, len(data) - 1]:
                outputs = self.outputs.copy()
                changed = bytearray(data)
                changed[offset] ^= 1
                outputs[name] = bytes(changed)
                record = copy.deepcopy(self.record)
                for case in record["cases"]:
                    if case["case_id"] == name:
                        case["actual_sha256"] = case["expected_sha256"] = sha256(outputs[name])
                with self.subTest(name=name, offset=offset), self.assertRaises(ValueError):
                    self.check(record, outputs)

    def test_missing_and_short_output(self):
        for name in self.outputs:
            for data in [b"", self.outputs[name][:-1]]:
                outputs = self.outputs.copy()
                outputs[name] = data
                with self.subTest(name=name), self.assertRaises(ValueError):
                    self.check(outputs=outputs)

    def test_completion_validation_and_controls_required(self):
        record = copy.deepcopy(self.record)
        record["cases"][0]["completion_values"] = [1, 1, 1]
        with self.assertRaises(ValueError):
            self.check(record)
        record = copy.deepcopy(self.record)
        record["controls"].pop()
        with self.assertRaises(ValueError):
            self.check(record)
        self.record["device"]["synchronization_validation"] = False
        with self.assertRaises(ValueError):
            self.check()


if __name__ == "__main__":
    unittest.main()
