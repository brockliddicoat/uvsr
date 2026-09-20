"""Execute ten source-fixture float readbacks. Original PNG parity is a separate gate."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid

from run_agfx_copy import FEATURES, ROOT, require, sha256, validation_messages
from run_log import RunLog

FIXTURES = {"GatherTest": "gather_cs", "ScatterTest": "scatter_cs", "TableTest": "table_cs",
            "2DTest": "two_d_cs", "3DTest": "world_cs"}
CASES = [(level, name) for level in (0, 3) for name in FIXTURES]
SOURCES = ["Cargo.toml", "Cargo.lock", "crates/agfx/Cargo.toml", "crates/agfx/src/lib.rs",
           "crates/agfx/src/vulkan.rs", "crates/agfx/src/vulkan/compute.rs",
           "crates/agfx/src/bin/shader_to_human.rs", "shaders/rust/shader_to_human_fixtures.rs",
           "tests/parity/fixtures/shader-to-human/camera.txt"]
SOURCES += [p.relative_to(ROOT).as_posix() for directory in ("src", "fixtures")
            for p in sorted((ROOT / "crates/shader-to-human" / directory).glob("*.rs"))]


def camera_root():
    values = [float(word) for word in (ROOT / "tests/parity/fixtures/shader-to-human/camera.txt").read_text().split()]
    require(len(values) == 20 and all(math.isfinite(value) for value in values), "invalid fixed camera")
    return struct.pack("<20f", *values)


def check_record(record, token, identity, artifact, level, name, actual):
    require(record.get("schema_version") == 1 and record.get("run_token") == token, "version or fresh token mismatch")
    require(record.get("case_id") == f"s2h.{name}.opt{level}" and record.get("status") == "pass", "case ID/status mismatch")
    require(record.get("host_source_sha256") == identity and record.get("shader") == artifact, "compiled identity mismatch")
    scatter = name == "ScatterTest"
    root = camera_root() if name == "3DTest" else b""
    require(record.get("optimization_level") == level and record.get("entry_point") == FIXTURES[name], "entry/level mismatch")
    require(record.get("dispatch_groups") == ([1, 1, 1] if scatter else [100, 75, 1]) and
            record.get("workgroup_size") == ([1, 1, 1] if scatter else [8, 8, 1]), "dispatch dimensions mismatch")
    require(record.get("root_bytes") == len(root) and record.get("root_sha256") == sha256(root), "root ABI/input mismatch")
    require(record.get("completion_values") == [1, 2, 3], "missing ordered completion")
    require(record.get("resolution") == [800, 600] and record.get("format") == "RGBA32_FLOAT", "pixel layout mismatch")
    require(record.get("output") == f"{name}-opt{level}.rgba32f", "output path mismatch")
    require(len(actual) == 800 * 600 * 16 and record.get("bytes") == len(actual), "incomplete readback")
    require(record.get("actual_sha256") == sha256(actual), "readback hash mismatch")
    require(any(value[0] != 0.0 for value in struct.iter_unpack("<f", actual)) and
            all(math.isfinite(value[0]) for value in struct.iter_unpack("<f", actual)), "nonfinite/empty output")
    require(record.get("golden_agreement") == "not checked", "execution must not imply golden agreement")
    device = record.get("device", {})
    require(device.get("validation") is True and device.get("synchronization_validation") is True, "validation disabled")
    require(min(device.get("api_version", 0), device.get("loader_api_version", 0)) >= (1 << 22 | 4 << 12)
            and set(device.get("enabled_features", [])) == FEATURES, "required device features missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--shader-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id="s2h.fixtures.float_readback", status="fail", run_token=token,
                  required=len(CASES), executed=0, passed=0, cases=[], golden_agreement="not checked")
    try:
        executable, sdk, shaders = args.executable.resolve(strict=True), args.sdk.resolve(strict=True), args.shader_dir.resolve(strict=True)
        identity = {path: sha256((ROOT / path).read_bytes()) for path in SOURCES}
        report.update(host_source_sha256=identity, executable_sha256=sha256(executable.read_bytes()),
                      validation_layer_sha256=sha256((sdk / "Bin/VkLayer_khronos_validation.dll").read_bytes()))
        compiled = subprocess.run([str(executable), "--identity"], capture_output=True, check=True, timeout=10)
        require(json.loads(compiled.stdout) == identity, "stale executable rejected before Vulkan")
        log.event("identity_checked")
        artifacts = {level: json.loads((shaders / f"fixtures_opt{level}.metadata.json").read_text()) for level in (0, 3)}
        for level, artifact in artifacts.items():
            require(artifact["payload_sha256"] == sha256((shaders / f"fixtures_opt{level}.spv").read_bytes()), "stale payload")
        environment = os.environ.copy()
        for name in ("VK_LAYER_ENABLES", "VK_LAYER_DISABLES", "VK_LAYER_SETTINGS_PATH"):
            environment.pop(name, None)
        environment.update(VK_LOADER_LAYERS_DISABLE="~implicit~", VK_LAYER_PATH=str(sdk / "Bin"),
                           VK_LOADER_DEBUG="layer", VK_LAYER_VALIDATE_CORE="1", VK_LAYER_VALIDATE_SYNC="1")
        environment["PATH"] = str(sdk / "Bin") + os.pathsep + environment["PATH"]
        report["environment"] = {key: value for key, value in environment.items() if key.startswith("VK_")}
        for level, name in CASES:
            folder = output / f"{name}-opt{level}"
            folder.mkdir(exist_ok=True)
            raw = folder / f"{name}-opt{level}.rgba32f"
            raw.unlink(missing_ok=True)
            command = [str(executable), "--run-token", token, "--shader-dir", str(shaders), "--level", str(level), "--case", name]
            log.event("native_started", case_id=f"s2h.{name}.opt{level}", command=command)
            # A process exit or timeout cannot prove whether a dispatch happened.
            # Preserve the already verified cases and leave this total unknown
            # until the complete native record and readback establish execution.
            report["executed"] = None
            try:
                result = subprocess.run(command, cwd=folder, env=environment, capture_output=True, timeout=120)
            except subprocess.TimeoutExpired as error:
                (folder / "native.stdout.txt").write_bytes(error.stdout or b"")
                (folder / "native.stderr.txt").write_bytes(error.stderr or b"")
                raise
            (folder / "native.stdout.txt").write_bytes(result.stdout)
            (folder / "native.stderr.txt").write_bytes(result.stderr)
            text, err = result.stdout.decode("utf-8", "replace"), result.stderr.decode("utf-8", "replace")
            diagnostics, notices = validation_messages(text + err)
            inserted = ('Insert instance layer "VK_LAYER_KHRONOS_validation"' in err and
                        'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err)
            require(result.returncode == 0 and inserted and not diagnostics,
                    f"{name} opt{level}: native exit/validation failed: {diagnostics[:3]}")
            record = json.loads(text)
            check_record(record, token, identity, artifacts[level], level, name, raw.read_bytes())
            report["cases"].append(dict(native=record, validation_inserted=inserted, intentional_loader_notices=notices))
            report["passed"] += 1
            report["executed"] = report["passed"]
            log.event("case_verified", case_id=record["case_id"])
            print(json.dumps({key: record[key] for key in ("case_id", "status", "actual_sha256", "golden_agreement")}), flush=True)
        report["status"] = "pass"
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    except KeyboardInterrupt:
        report.update(status="incomplete", failure="interrupted")
    report["elapsed_seconds"] = time.monotonic() - start
    log.finish(report)
    print(json.dumps({key: report.get(key) for key in ("case_id", "status", "failure", "required", "executed", "passed", "golden_agreement", "elapsed_seconds")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    raise SystemExit(main())
