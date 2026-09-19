"""Verify incomplete phase evidence cannot masquerade as a completed pass."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import run_agfx_copy
import run_agfx_compute
from run_log import RunLog


class PhaseRecords(unittest.TestCase):
    def test_both_runners_publish_failed_timeout_with_unknown_execution(self):
        for runner in (run_agfx_copy, run_agfx_compute):
            with self.subTest(runner=runner.__name__), tempfile.TemporaryDirectory() as directory:
                base = Path(directory)
                self.assertTrue(base.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve()))
                exe = base / "fake.exe"; exe.write_bytes(b"never executed")
                sdk = base / "sdk"; (sdk / "Bin").mkdir(parents=True)
                (sdk / "Bin/VkLayer_khronos_validation.dll").write_bytes(b"never loaded")
                output = base / "output"; output.mkdir()
                (output / "result.json").write_text('{"status":"pass","run_token":"old"}')
                identity = {name: runner.sha256((runner.ROOT / name).read_bytes()) for name in runner.SOURCES}
                args = [runner.__name__, "--executable", str(exe), "--sdk", str(sdk), "--output-dir", str(output)]
                if runner is run_agfx_compute:
                    shaders = base / "shaders"; shaders.mkdir()
                    for level in (0,3):
                        (shaders / f"compute_multi_dispatch_opt{level}.spv").write_bytes(b"not a GPU module")
                        data = {"payload_sha256": runner.sha256(b"not a GPU module"), "identity": {"source_sha256": identity["shaders/rust/compute_multi_dispatch.rs"]}}
                        (shaders / f"compute_multi_dispatch_opt{level}.metadata.json").write_text(json.dumps(data))
                    args += ["--shader-dir", str(shaders)]
                calls = [subprocess.CompletedProcess([], 0, json.dumps(identity).encode(), b""), subprocess.TimeoutExpired([], 45, output=b"partial stdout", stderr=b"partial stderr")]
                with patch("sys.argv", args), patch.object(runner.subprocess, "run", side_effect=calls), patch("builtins.print"):
                    self.assertTrue(runner.main())
                record = json.loads((output / "result.json").read_text())
                self.assertEqual(record["status"], "fail")
                self.assertTrue(record["timed_out"])
                self.assertIsNone(record["executed"])
                self.assertEqual(record["passed"], 0)
                self.assertNotEqual(record["run_token"], "old")
                self.assertEqual((output / "native.stderr.txt").read_bytes(), b"partial stderr")
                self.assertFalse((output / "result.json.partial").exists())

    def test_missing_executable_invalidates_old_pass_before_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            self.assertTrue(output.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve()))
            (output / "result.json").write_text('{"status":"pass"}')
            args = ["copy", "--executable", str(output / "missing.exe"), "--sdk", str(output), "--output-dir", str(output)]
            with patch("sys.argv", args), self.assertRaises(FileNotFoundError): run_agfx_copy.main()
            self.assertFalse((output / "result.json").exists())
            self.assertEqual(json.loads((output / "events.jsonl").read_text())["phase"], "started")

    def test_interrupted_native_has_phase_evidence_without_result(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            self.assertTrue(output.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve()))
            log = RunLog(output, "fresh")
            log.event("native_started", command=["fixture"])
            events = [json.loads(line) for line in log.path.read_text().splitlines()]
            self.assertEqual([event["phase"] for event in events], ["started", "native_started"])
            self.assertTrue(all(event["run_token"] == "fresh" for event in events))
            self.assertFalse((output / "result.json").exists())

    def test_complete_record_is_replaced_without_partial_file(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            self.assertTrue(output.resolve().is_relative_to(Path(tempfile.gettempdir()).resolve()))
            log = RunLog(output, "fresh")
            (output / "result.json").write_text('{"status":"pass","run_token":"old"}')
            report = {"status": "incomplete", "run_token": "fresh", "required": 4, "executed": None, "passed": 0}
            log.finish(report)
            self.assertEqual(json.loads((output / "result.json").read_text()), report)
            self.assertFalse((output / "result.json.partial").exists())


if __name__ == "__main__": unittest.main()
