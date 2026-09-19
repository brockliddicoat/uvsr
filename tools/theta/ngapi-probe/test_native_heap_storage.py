"""CPU controls for the storage-image oracle, required cases and runner failures."""
from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_native_heap_storage as runner


def fixture():
    metadata = {variant: {"identity": {"source_sha256": "source"}, "payload_sha256": variant} for variant in runner.VARIANTS}
    records = [{"case_id": runner.PREFIX + "controls", "status": "pass", "checks": 169, "shader_cases_executed": 0}]
    for variant in runner.VARIANTS:
        for phase in range(4):
            records.append(dict(case_id=runner.PREFIX + variant + f".phase{phase}", status="pass",
                                source_sha256="source", payload_sha256=variant, output_address="0x1234567800000040",
                                source_index=1 if phase % 2 == 0 else 3, destination_index=3 if phase % 2 == 0 else 1,
                                image_descriptor_bytes=32, heap_slots=4, completion=phase + 1,
                                first_mismatch_word=-1, shader_cases_executed=1, actual=runner.expected_words(phase)))
    return records, metadata


class Records(unittest.TestCase):
    def test_valid_and_known_pixel_arithmetic(self):
        runner.check_records(*fixture())
        self.assertEqual(runner.expected_words(0)[:4], [0x12345685, 0x12345689, 0x1234568d, 0x12345691])
        self.assertEqual(runner.expected_words(2)[:4], [0xfffffffd, 1, 5, 9])
        self.assertEqual(runner.expected_words(3)[:4], [0xfd, 0x101, 0x105, 0x109])
        self.assertEqual(runner.expected_words(2)[4:8], [0xa5c37e19] * 4)
        self.assertEqual(runner.expected_words(2)[12:16], [0xfffffffc, 0xffffffff, 2, 5])
        self.assertEqual(runner.expected_words(2)[32:36], [0xfffffffd, 1, 5, 9])

    def test_every_readback_byte(self):
        records, metadata = fixture()
        for row in range(1, 17):
            for byte in range(160):
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

    def test_identity_selection_ranges_and_counts(self):
        records, metadata = fixture()
        changes = {"source_sha256": "stale", "payload_sha256": "stale", "output_address": "0",
                   "source_index": 3, "destination_index": 1, "image_descriptor_bytes": 0, "heap_slots": 3,
                   "completion": 0, "shader_cases_executed": 0, "first_mismatch_word": 0}
        for key, value in changes.items():
            changed = deepcopy(records)
            changed[1][key] = value
            with self.assertRaises(ValueError, msg=key):
                runner.check_records(changed, metadata)
        for address in ("0x1", "0xfffffffffffffff0"):
            changed = deepcopy(records)
            changed[1]["output_address"] = address
            with self.assertRaises(ValueError):
                runner.check_records(changed, metadata)
        records[0]["checks"] = 168
        with self.assertRaises(ValueError):
            runner.check_records(records, metadata)

    def test_timeout_keeps_unknown_execution_and_streams(self):
        with tempfile.TemporaryDirectory(prefix="theta-storage-") as directory:
            root = Path(directory).resolve()
            self.assertTrue(root.is_relative_to(Path(tempfile.gettempdir()).resolve()))
            output, shaders, sdk = root / "out", root / "shaders", root / "sdk"
            output.mkdir(); shaders.mkdir(); (sdk / "Bin").mkdir(parents=True)
            (sdk / "Bin/VkLayer_khronos_validation.dll").write_bytes(b"layer")
            executable = root / "probe.exe"; executable.write_bytes(b"executable")
            source = root / "tests/compiletests/ui/descriptor_heap/auxiliary/storage_body.rs"
            source.parent.mkdir(parents=True); source.write_bytes(b"source")
            identity = {"source_sha256": runner.sha256(source),
                        "host_sha256": runner.sha256(Path(runner.__file__).with_name("native_heap_storage.cpp")), "payloads": {}}
            for variant in runner.VARIANTS:
                stem = shaders / ("native_heap_storage_" + variant)
                stem.with_suffix(".spv").write_bytes(variant.encode())
                data = dict(schema_version=1, status="pass", variant=variant, language="Rust", payload_type="SPIR-V",
                            stage="compute", entry_point="computeMain", profile="ngapi-native-heap-storage",
                            target="spirv-unknown-vulkan1.3-physical64", root_bytes=16, output_bytes=32,
                            texture_extent=[2, 2], texture_format="rgba32_uint", workgroup_size=[1, 1, 1],
                            optimization_level=int(variant[-1]), spirt_passes=["qptr"] if variant.startswith("qptr") else [],
                            capabilities=sorted(runner.CAPABILITIES), extensions=sorted(runner.EXTENSIONS),
                            identity={"source_sha256": identity["source_sha256"]}, payload_sha256=runner.sha256(stem.with_suffix(".spv")))
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
