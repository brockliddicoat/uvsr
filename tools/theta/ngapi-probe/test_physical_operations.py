"""CPU-only controls for the aggregate/alias execution record."""
from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_physical_operations as runner


def fixture():
    metadata = {v: {"identity": {"source_sha256": "source"}, "payload_sha256": v} for v in runner.VARIANTS}
    records = [{"case_id": runner.PREFIX + "controls", "status": "pass", "checks": 25, "shader_cases_executed": 0}]
    for variant in runner.VARIANTS:
        for index, seed in enumerate(runner.SEEDS):
            records.append({"case_id": runner.PREFIX + variant + f".seed{index}", "status": "pass",
                            "source_sha256": "source", "payload_sha256": variant, "source_address": "0x1234567800000020",
                            "destination_address": "0x1234567800000040", "alias_address": "0x1234567800000040",
                            "seed": seed, "completion": index + 1, "first_mismatch_word": -1,
                            "nonzero_high_address_bits": True, "shader_cases_executed": 1,
                            "actual": runner.expected_words(seed, 0x1234567800000040)})
    return records, metadata


class Records(unittest.TestCase):
    def test_valid(self):
        runner.check_records(*fixture())

    def test_known_recurrence_and_guard(self):
        words = runner.expected_words(0xfffffffc, 0x1234567800000040)
        self.assertEqual(words[:3], [0xfffffffd, 0xaa55aa58, 18])
        self.assertEqual(words[3], 0xaa55aa67)
        self.assertEqual(words[4:8], [64, 0x12345678, 0x31210ff1, 0xa5c37e19])

    def test_every_readback_byte(self):
        records, metadata = fixture()
        for row in range(1, 13):
            for byte in range(64):
                changed = deepcopy(records)
                changed[row]["actual"][byte // 4] ^= 1 << (8 * (byte % 4))
                with self.assertRaises(ValueError):
                    runner.check_records(changed, metadata)

    def test_missing_duplicate_skipped(self):
        records, metadata = fixture()
        for changed in ([], records[:-1], records[:-1] + [records[1]], records + [records[1]]):
            with self.assertRaises(ValueError):
                runner.check_records(changed, metadata)
        records[1]["status"] = "skip"
        with self.assertRaises(ValueError):
            runner.check_records(records, metadata)

    def test_identity_ranges_and_counts(self):
        records, metadata = fixture()
        changes = {"source_sha256": "stale", "payload_sha256": "stale", "source_address": "0",
                   "destination_address": "0x1234567800000028", "alias_address": "0x1234567800000048",
                   "seed": 9, "completion": 0, "shader_cases_executed": 0,
                   "first_mismatch_word": 0, "nonzero_high_address_bits": False}
        for key, value in changes.items():
            changed = deepcopy(records)
            changed[1][key] = value
            with self.assertRaises(ValueError, msg=key):
                runner.check_records(changed, metadata)
        for value in ("0x4", "0xfffffffffffffff8"):
            changed = deepcopy(records)
            changed[1]["source_address"] = value
            with self.assertRaises(ValueError):
                runner.check_records(changed, metadata)
        records[0]["checks"] = 24
        with self.assertRaises(ValueError):
            runner.check_records(records, metadata)

    def test_timeout_keeps_unknown_execution_and_streams(self):
        with tempfile.TemporaryDirectory(prefix="theta-operations-") as directory:
            root = Path(directory).resolve()
            self.assertTrue(root.is_relative_to(Path(tempfile.gettempdir()).resolve()))
            output, shaders, sdk = root / "out", root / "shaders", root / "sdk"
            output.mkdir(); shaders.mkdir(); (sdk / "Bin").mkdir(parents=True)
            (sdk / "Bin/VkLayer_khronos_validation.dll").write_bytes(b"layer")
            executable = root / "probe.exe"; executable.write_bytes(b"executable")
            source = root / "tests/compiletests/ui/physical_storage/auxiliary/operations_body.rs"
            source.parent.mkdir(parents=True); source.write_bytes(b"source")
            identity = {"source_sha256": runner.sha256(source),
                        "host_sha256": runner.sha256(Path(runner.__file__).with_name("physical_operations.cpp")),
                        "root_sha256": runner.sha256(Path(runner.__file__).with_name("physical_readback.hpp")), "payloads": {}}
            for variant in runner.VARIANTS:
                stem = shaders / ("physical_operations_" + variant)
                stem.with_suffix(".spv").write_bytes(variant.encode())
                data = {"status": "pass", "variant": variant, "stage": "compute", "entry_point": "computeMain",
                        "profile": "ngapi-physical-operations", "target": "spirv-unknown-vulkan1.3-physical64",
                        "root_bytes": 16, "source_bytes": 24, "allocation_bytes": 32, "workgroup_size": [1, 1, 1],
                        "optimization_level": int(variant[-1]), "spirt_passes": ["qptr"] if variant.startswith("qptr") else [],
                        "identity": {"source_sha256": identity["source_sha256"]}, "payload_sha256": runner.sha256(stem.with_suffix(".spv"))}
                identity["payloads"][variant] = data["payload_sha256"]
                stem.with_suffix(".metadata.json").write_text(json.dumps(data))
            (output / "result.json").write_text('{"status":"pass"}')
            partial = json.dumps(fixture()[0][0]).encode() + b"\n"
            responses = [subprocess.CompletedProcess([], 0, json.dumps(identity).encode(), b""),
                         subprocess.TimeoutExpired([], 45, output=partial, stderr=b"first failure")]
            arguments = ["run", "--executable", str(executable), "--sdk", str(sdk), "--shader-dir", str(shaders),
                         "--upstream", str(root), "--output-dir", str(output)]
            with patch("sys.argv", arguments), patch.object(runner.subprocess, "run", side_effect=responses), redirect_stdout(io.StringIO()):
                self.assertEqual(runner.main(), 1)
            result = json.loads((output / "result.json").read_text())
            self.assertEqual((result["status"], result["passed"], result["executed"], result["reported_cases"]), ("fail", 0, None, 0))
            self.assertEqual((output / "stderr.txt").read_bytes(), b"first failure")
            self.assertEqual((output / "stdout.txt").read_bytes(), partial)
            self.assertIn('"native_started"', (output / "events.jsonl").read_text())


if __name__ == "__main__":
    unittest.main()
