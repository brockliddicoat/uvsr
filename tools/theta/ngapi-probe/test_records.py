"""CPU-only negative controls for the consumer's result gate."""

import copy
import unittest

from run_readback import check_heap_records, check_records


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


class HeapRecords(unittest.TestCase):
    def setUp(self):
        self.metadata = {opt: {"identity": {"source_sha256": "source"}, "payload_sha256": f"module{opt}"} for opt in [0, 3]}
        self.rows = [{"case_id": "theta.m2.ngapi.native_heap_sample.controls", "status": "pass", "checks": 74, "shader_cases_executed": 0}]
        colors = [[0x40000000, 0, 0, 0x40000000], [0, 0x40000000, 0, 0x40000000],
                  [0, 0, 0x40000000, 0x40000000], [0x40000000] * 4]
        tail = [0xa5c37e19] * 4 + [0xff0000ff, 0xff00ff00, 0xff000000, 0xff00ffff, 0xffff0000, 0xffffffff, 0xff000000, 0xffff00ff]
        for opt in [0, 3]:
            for index, (image, sampler) in enumerate([(1, 2), (1, 3), (3, 2), (3, 3)]):
                self.rows.append({
                    "case_id": f"theta.m2.ngapi.native_heap_sample.opt{opt}.resource{image}.sampler{sampler}", "status": "pass",
                    "source_sha256": "source", "payload_sha256": f"module{opt}", "output_address": "0x1000",
                    "nonzero_high_address_bits": False, "resource_index": image, "sampler_index": sampler,
                    "image_descriptor_bytes": 32, "sampler_descriptor_bytes": 16, "heap_slots": 4, "root_bytes": 16,
                    "first_mismatch_word": -1, "shader_cases_executed": 1, "actual": colors[index] + tail,
                })

    def test_complete_matrix(self):
        check_heap_records(self.rows, self.metadata)

    def test_empty_missing_duplicate_cases(self):
        for rows in [[], self.rows[:-1], self.rows + [self.rows[-1]], self.rows[:-1] + [self.rows[0]]]:
            with self.subTest(count=len(rows)), self.assertRaises(ValueError):
                check_heap_records(rows, self.metadata)

    def test_every_color_guard_and_texture_word(self):
        for index in range(16):
            rows = copy.deepcopy(self.rows)
            rows[1]["actual"][index] ^= 1
            with self.subTest(word=index), self.assertRaises(ValueError):
                check_heap_records(rows, self.metadata)

    def test_stale_identity_wrong_slot_or_incomplete_run(self):
        for field, value in [("source_sha256", "stale"), ("payload_sha256", "stale"), ("resource_index", 0), ("sampler_index", 0),
                             ("root_bytes", 8), ("heap_slots", 3), ("shader_cases_executed", 0), ("status", "blocked"),
                             ("first_mismatch_word", 0), ("image_descriptor_bytes", 0), ("sampler_descriptor_bytes", 0),
                             ("image_descriptor_bytes", 64), ("output_address", "0x0"), ("output_address", "0x1004"),
                             ("output_address", "0xfffffffffffffff0"), ("nonzero_high_address_bits", True)]:
            rows = copy.deepcopy(self.rows)
            rows[1][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                check_heap_records(rows, self.metadata)

    def test_wrong_color_for_selected_sampler(self):
        self.rows[1]["actual"] = self.rows[2]["actual"]
        with self.assertRaises(ValueError):
            check_heap_records(self.rows, self.metadata)

    def test_incomplete_cpu_controls(self):
        self.rows[0]["checks"] -= 1
        with self.assertRaises(ValueError):
            check_heap_records(self.rows, self.metadata)


if __name__ == "__main__":
    unittest.main()
