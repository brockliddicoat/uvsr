import copy
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from cube_oracle import check_cube_records, reference
import run_native_heap_mesh as runner


class MeshRecords(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="theta-mesh-records-")
        self.addCleanup(directory.cleanup)
        self.output = Path(directory.name)
        self.metadata = {v: {"identity": {"source_sha256": "source"}, "payload_sha256": v} for v in runner.VARIANTS}
        self.rows = [dict(case_id=runner.PREFIX + "controls", status="pass", checks=10, shader_cases_executed=0)]
        for variant in runner.VARIANTS:
            for case in range(4):
                stem = f"mesh.{variant}.case{case}"
                color, depth, _, _ = reference(case)
                (self.output / (stem + ".rgba")).write_bytes(color)
                (self.output / (stem + ".depth")).write_bytes(struct.pack("<16384f", *depth))
                self.rows.append(dict(case_id=f"{runner.PREFIX}{variant}.case{case}", status="pass",
                    source_sha256="source", payload_sha256=variant, view=case//2,
                    resource_index=1 if case < 2 else 3, sampler_index=2+case%2, vertex_address="0x1000",
                    nonzero_high_address_bits=False, vertex_count=24, vertex_stride=24, index_count=36,
                    root_bytes=80, width=128, height=128, image_descriptor_bytes=32, sampler_descriptor_bytes=16,
                    heap_slots=4, first_input_mismatch_byte=-1, guard_intact=True, shader_cases_executed=1,
                    completion_value=case+1, task_group_count=[1, 1, 1], mesh_group_count=[6, 1, 1],
                    mesh_output_vertices=4, mesh_output_primitives=2, color_file=stem+".rgba", depth_file=stem+".depth"))

    def test_complete_matrix(self):
        check_cube_records(self.rows, self.metadata, self.output, mesh=True)
        self.assertEqual(len(list(self.output.glob("*.actual.png"))), 16)

    def test_incomplete_duplicate_skipped_or_stale(self):
        for rows in ([], self.rows[:-1], self.rows + [self.rows[-1]], self.rows[:-1] + [self.rows[0]]):
            with self.assertRaises(ValueError):
                check_cube_records(rows, self.metadata, self.output, mesh=True)
        for field, value in (("status", "blocked"), ("shader_cases_executed", 0), ("payload_sha256", "stale"),
                             ("source_sha256", "stale"), ("completion_value", 0), ("vertex_stride", 32),
                             ("mesh_group_count", [1, 1, 1]), ("mesh_output_vertices", 24),
                             ("first_input_mismatch_byte", 1), ("guard_intact", False),
                             ("color_file", "../outside.rgba"), ("depth_file", "mesh.qptr_opt3.case0.depth")):
            rows = copy.deepcopy(self.rows)
            rows[1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_cube_records(rows, self.metadata, self.output, mesh=True)

    def test_missing_and_wrong_stage_image(self):
        image = self.output / self.rows[1]["color_file"]
        image.write_bytes(bytes((17, 34, 51, 255)) * 16384)
        with self.assertRaises(ValueError):
            check_cube_records(self.rows, self.metadata, self.output, mesh=True)
        image.unlink()
        with self.assertRaises(OSError):
            check_cube_records(self.rows, self.metadata, self.output, mesh=True)


class MeshRunner(unittest.TestCase):
    def test_preflight_rejections_and_timeout_record(self):
        with tempfile.TemporaryDirectory(prefix="theta-mesh-timeout-") as directory:
            root = Path(directory)
            output, shaders, sdk = root / "out", root / "shaders", root / "sdk"
            output.mkdir(); shaders.mkdir(); (sdk / "Bin").mkdir(parents=True)
            (sdk / "Bin/VkLayer_khronos_validation.dll").write_bytes(b"layer")
            executable = root / "probe.exe"; executable.write_bytes(b"executable")
            source = root / "tests/compiletests/ui/descriptor_heap/auxiliary/mesh_body.rs"
            source.parent.mkdir(parents=True); source.write_bytes(b"source")
            identity = dict(profile="ngapi-native-heap-mesh", source_sha256=runner.sha256(source),
                            host_sha256=runner.sha256(Path(runner.__file__).with_name("native_heap_cube.cpp")),
                            abi_sha256=runner.sha256(Path(runner.__file__).with_name("native_heap_cube.hpp")), payload_sha256=[])
            for variant in runner.VARIANTS:
                stem = shaders / ("native_heap_mesh_" + variant)
                stem.with_suffix(".spv").write_bytes(variant.encode())
                data = dict(schema_version=1, status="pass", variant=variant, language="Rust", payload_type="SPIR-V",
                            entry_points=runner.ENTRIES, profile="ngapi-native-heap-mesh",
                            target="spirv-unknown-vulkan1.3-physical64", root_bytes=80, task_payload_bytes=80,
                            vertex_count=24, vertex_stride=24, primitive_count=12, workgroup_size=[1, 1, 1],
                            task_group_count=[1, 1, 1], mesh_group_count=[6, 1, 1], mesh_output_vertices=4, mesh_output_primitives=2,
                            optimization_level=int(variant[-1]), spirt_passes=["qptr"] if variant.startswith("qptr") else [],
                            capabilities=sorted(runner.CAPABILITIES), extensions=sorted(runner.EXTENSIONS),
                            identity={"source_sha256": identity["source_sha256"]}, payload_sha256=runner.sha256(stem.with_suffix(".spv")))
                identity["payload_sha256"].append(data["payload_sha256"])
                stem.with_suffix(".metadata.json").write_text(json.dumps(data))
            metadata_path = shaders / "native_heap_mesh_default_opt0.metadata.json"
            metadata = json.loads(metadata_path.read_text())
            arguments = ["run", "--executable", str(executable), "--sdk", str(sdk), "--shader-dir", str(shaders),
                         "--upstream", str(root), "--output-dir", str(output)]
            for field, value in (("entry_points", []), ("capabilities", []), ("task_payload_bytes", 16),
                                 ("vertex_count", 25), ("primitive_count", 13), ("workgroup_size", [2, 1, 1]),
                                 ("task_group_count", [6, 1, 1]), ("mesh_group_count", [1, 1, 1]),
                                 ("mesh_output_vertices", 24), ("mesh_output_primitives", 12),
                                 ("spirt_passes", ["qptr"]), ("payload_sha256", "stale")):
                metadata_path.write_text(json.dumps(dict(metadata, **{field: value})))
                with patch("sys.argv", arguments), patch.object(runner.subprocess, "run", return_value=
                        subprocess.CompletedProcess([], 0, json.dumps(identity).encode(), b"")) as execute, redirect_stdout(io.StringIO()):
                    self.assertEqual(runner.main(), 1)
                    self.assertEqual(execute.call_count, 1)
                    self.assertEqual(json.loads((output / "result.json").read_text())["executed"], 0)
            metadata_path.write_text(json.dumps(metadata))
            partial = json.dumps(dict(case_id=runner.PREFIX+"controls", status="pass", checks=10, shader_cases_executed=0)).encode()+b"\n"
            for failure in (subprocess.TimeoutExpired([], 45, output=partial, stderr=b"first failure"),
                            subprocess.CompletedProcess([], 0xc0000005, partial, b"first failure")):
                responses = [subprocess.CompletedProcess([], 0, json.dumps(identity).encode(), b""), failure]
                with patch("sys.argv", arguments), patch.object(runner.subprocess, "run", side_effect=responses), redirect_stdout(io.StringIO()):
                    self.assertEqual(runner.main(), 1)
                result = json.loads((output / "result.json").read_text())
                self.assertEqual((result["status"], result["passed"], result["executed"], result["reported_cases"]), ("fail", 0, None, 0))
                self.assertEqual((output / "stderr.txt").read_bytes(), b"first failure")
                self.assertEqual((output / "stdout.txt").read_bytes(), partial)
                self.assertIn('"native_started"', (output / "events.jsonl").read_text())


if __name__ == "__main__":
    unittest.main()
