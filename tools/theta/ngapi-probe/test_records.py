"""CPU-only negative controls for the consumer's result gate."""

import copy
import unittest

from run_readback import check_records


class ReadbackRecords(unittest.TestCase):
    def setUp(self):
        self.metadata = {opt: {"identity": {"source_sha256": "source"}, "payload_sha256": f"module{opt}"} for opt in [0, 3]}
        self.rows = [{"case_id": "theta.m2.ngapi.physical_readback.controls", "status": "pass", "checks": 16, "shader_cases_executed": 0}]
        for opt in [0, 3]:
            for index, seed in enumerate([0, 0x12345678, 0xfffffffc]):
                self.rows.append({
                    "case_id": f"theta.m2.ngapi.physical_readback.opt{opt}.seed{index}", "status": "pass",
                    "source_sha256": "source", "payload_sha256": f"module{opt}", "source_address": "0x1000",
                    "seed": seed, "first_mismatch_word": -1, "shader_cases_executed": 1,
                    "nonzero_high_address_bits": False,
                    "actual": [(seed + 7) & 0xffffffff, 0x1000, 0, 0xa5c37e19, seed, 0xa5c37e19, 0xa5c37e19, 0xa5c37e19],
                })

    def test_complete_low_address_run(self):
        check_records(self.rows, self.metadata)

    def test_complete_high_address_run(self):
        for row in self.rows[1:]:
            row.update(source_address="0x1234567800001000", nonzero_high_address_bits=True)
            row["actual"][2] = 0x12345678
        check_records(self.rows, self.metadata)

    def test_empty_missing_duplicate_or_unexpected_cases(self):
        for rows in [[], self.rows[:-1], self.rows + [self.rows[-1]], self.rows[:-1] + [self.rows[0]]]:
            with self.subTest(count=len(rows)), self.assertRaises(ValueError):
                check_records(rows, self.metadata)

    def test_stale_source_or_module(self):
        for field in ["source_sha256", "payload_sha256"]:
            rows = copy.deepcopy(self.rows)
            rows[1][field] = "stale"
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_records(rows, self.metadata)

    def test_each_corrupted_word(self):
        for index in range(8):
            rows = copy.deepcopy(self.rows)
            rows[1]["actual"][index] ^= 1
            with self.subTest(word=index), self.assertRaises(ValueError):
                check_records(rows, self.metadata)

    def test_incomplete_status_or_denominator(self):
        for field, value in [("status", "blocked"), ("shader_cases_executed", 0), ("first_mismatch_word", 1)]:
            rows = copy.deepcopy(self.rows)
            rows[1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_records(rows, self.metadata)

    def test_fabricated_high_address_coverage(self):
        self.rows[1]["nonzero_high_address_bits"] = True
        with self.assertRaises(ValueError):
            check_records(self.rows, self.metadata)

    def test_incomplete_cpu_controls(self):
        self.rows[0]["checks"] = 0
        with self.assertRaises(ValueError):
            check_records(self.rows, self.metadata)


if __name__ == "__main__":
    unittest.main()
