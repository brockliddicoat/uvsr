"""Validate sixteen task/mesh draws with the existing cube color/depth oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

from compile_native_heap_mesh import CAPABILITIES, EXTENSIONS, ENTRIES
from cube_oracle import check_cube_records
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_log import RunLog

PREFIX = "theta.m2.ngapi.native_heap_mesh."
VARIANTS = [f"{pipeline}_opt{opt}" for pipeline in ("default", "qptr") for opt in (0, 3)]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("executable", "sdk", "shader-dir", "upstream", "output-dir"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in ("result.json", "stdout.txt", "stderr.txt"):
        (output / name).unlink(missing_ok=True)
    log = RunLog(output, str(uuid.uuid4()))
    report = dict(schema_version=1, case_id=PREFIX + "run", status="fail", required=16, executed=0, passed=0)
    try:
        exe, sdk = args.executable.resolve(strict=True), args.sdk.resolve(strict=True)
        source = args.upstream.resolve() / "tests/compiletests/ui/descriptor_heap/auxiliary/mesh_body.rs"
        identity = {"source_sha256": sha256(source), "host_sha256": sha256(Path(__file__).with_name("native_heap_cube.cpp")),
                    "abi_sha256": sha256(Path(__file__).with_name("native_heap_cube.hpp")), "profile": "ngapi-native-heap-mesh"}
        compiled = subprocess.run([str(exe), "--identity"], capture_output=True, timeout=10, check=True)
        metadata = {}
        for variant in VARIANTS:
            stem = args.shader_dir / ("native_heap_mesh_" + variant)
            data = json.loads(stem.with_suffix(".metadata.json").read_text())
            require(data["schema_version"] == 1 and data["status"] == "pass" and data["variant"] == variant
                    and data["language"] == "Rust" and data["payload_type"] == "SPIR-V"
                    and data["entry_points"] == ENTRIES
                    and data["profile"] == "ngapi-native-heap-mesh" and data["target"] == "spirv-unknown-vulkan1.3-physical64"
                    and data["root_bytes"] == 80 and data["task_payload_bytes"] == 80
                    and data["vertex_count"] == 24 and data["vertex_stride"] == 24 and data["primitive_count"] == 12
                    and data["task_group_count"] == [1, 1, 1] and data["mesh_group_count"] == [6, 1, 1]
                    and data["mesh_output_vertices"] == 4 and data["mesh_output_primitives"] == 2
                    and data["optimization_level"] == int(variant[-1])
                    and data["spirt_passes"] == (["qptr"] if variant.startswith("qptr") else [])
                    and data["workgroup_size"] == [1, 1, 1] and set(data["capabilities"]) == CAPABILITIES
                    and set(data["extensions"]) == EXTENSIONS and data["identity"]["source_sha256"] == identity["source_sha256"]
                    and data["payload_sha256"] == sha256(stem.with_suffix(".spv")), "shader metadata mismatch")
            metadata[variant] = data
        identity["payload_sha256"] = [data["payload_sha256"] for data in metadata.values()]
        require(json.loads(compiled.stdout) == identity, "stale compiled host, shader or source identity")
        environment = os.environ.copy()
        for key in ("VK_LAYER_ENABLES", "VK_LAYER_DISABLES"):
            environment.pop(key, None)
        environment.update(VK_LOADER_LAYERS_DISABLE="~implicit~", VK_LAYER_PATH=str(sdk / "Bin"),
                           VK_INSTANCE_LAYERS="VK_LAYER_KHRONOS_validation", VK_LOADER_DEBUG="layer",
                           VK_LAYER_VALIDATE_CORE="1", VK_LAYER_VALIDATE_SYNC="1")
        report.update(identity=identity, executable_sha256=sha256(exe), metadata=metadata, command=[str(exe)],
                      validation_layer_sha256=sha256(sdk / "Bin/VkLayer_khronos_validation.dll"),
                      environment={key: value for key, value in environment.items() if key.startswith("VK_")})
        for variant in VARIANTS:
            for case in range(4):
                for suffix in ("rgba", "depth"):
                    (output / f"mesh.{variant}.case{case}.{suffix}").unlink(missing_ok=True)
        log.event("preflight_verified")
        report["executed"] = None
        log.event("native_started")
        try:
            native = subprocess.run([str(exe)], cwd=output, env=environment, capture_output=True, timeout=45)
            stdout, stderr, code = native.stdout, native.stderr, native.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code = error.stdout or b"", error.stderr or b"", None
        (output / "stdout.txt").write_bytes(stdout)
        (output / "stderr.txt").write_bytes(stderr)
        report["exit_code"] = code
        log.event("native_finished", exit_code=code)
        out_text, err_text = stdout.decode(errors="replace"), stderr.decode(errors="replace")
        inserted = ('Insert instance layer "VK_LAYER_KHRONOS_validation"' in err_text
                    and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err_text)
        diagnostics = re.findall(r"[^\r\n]*(?:Validation (?:Error|Warning)|NoGraphicsAPI validation:|VUID-|SYNC-HAZARD|ERROR \|)[^\r\n]*", out_text + err_text)
        records = [json.loads(line) for line in out_text.splitlines() if line.strip()]
        if records:
            reported = sum(row.get("shader_cases_executed", 0) for row in records)
            report["reported_cases"] = reported
            report["executed"] = reported if code == 0 else None
        adapter = re.search(r'Using "([^"]+)" with driver: "([^"]+)"', err_text)
        report.update(cases=records, validation_inserted=inserted, validation_diagnostics=diagnostics,
                      adapter=adapter[1] if adapter else None, driver_path=adapter[2] if adapter else None,
                      stdout_sha256=hashlib.sha256(stdout).hexdigest(), stderr_sha256=hashlib.sha256(stderr).hexdigest())
        check_cube_records(records, metadata, output, mesh=True)
        require(code == 0 and inserted and not diagnostics, "native exit or Vulkan validation failed")
        report.update(status="pass", passed=16,
                      nonzero_high_address_bits_covered=any(int(row.get("vertex_address", "0"), 16) >> 32 for row in records))
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    log.finish(report)
    print(json.dumps({key: report.get(key) for key in ("status", "required", "executed", "passed", "failure")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
