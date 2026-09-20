"""Validate four ordinary Rust shader cases and every unselected descriptor byte."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

from run_agfx_copy import FEATURES, ROOT, require, sha256, validation_messages
from run_log import RunLog

SOURCES = ["Cargo.toml", "Cargo.lock", "crates/agfx/Cargo.toml", "crates/agfx/src/lib.rs",
           "crates/agfx/src/vulkan.rs", "crates/agfx/src/vulkan/compute.rs",
           "crates/agfx/src/vulkan/ownership.rs", "crates/agfx/src/vulkan/texture.rs", "crates/agfx/src/bin/multi_dispatch.rs",
           "shaders/rust/compute_multi_dispatch.rs", "tests/parity/fixtures/agfx/compute_multi_dispatch_buffer.bin"]
CASES = [(level, slot) for level in (0, 3) for slot in (1, 3)]
CONTROLS = {f"agfx.compute.control.{name}.opt{level}" for name in ("uninitialized", "slot", "size", "role", "device") for level in (0, 3)}


def expected_bytes(resource, golden):
    return b"".join(golden if slot == resource else b"".join((0xa0000000 + slot * 0x1000 + i).to_bytes(4, "little") for i in range(64)) for slot in range(4))


def check_record(record, token, identity, artifacts, outputs, golden):
    require(record.get("schema_version") == 1 and record.get("run_token") == token and record.get("status") == "pass", "version, fresh token or native status mismatch")
    require(record.get("host_source_sha256") == identity and record.get("shaders") == artifacts, "compiled source or shader identity mismatch")
    require(all(type(record.get(key)) is int and record[key] == 4 for key in ("required", "executed", "passed")), "required compute denominator mismatch")
    cases = record.get("cases", [])
    require(len(cases) == 4, "missing or duplicated compute cases")
    require({case.get("case_id") for case in cases} == {f"agfx.compute_multi_dispatch_buffer.opt{level}.slot{slot}" for level, slot in CASES}, "wrong or duplicate case IDs")
    for index, (case, (level, slot)) in enumerate(zip(cases, CASES)):
        require(case.get("case_id") == f"agfx.compute_multi_dispatch_buffer.opt{level}.slot{slot}" and case.get("status") == "pass", "case order or status mismatch")
        require(case.get("optimization_level") == level and case.get("resource") == slot and case.get("passes") == 4, "wrong dispatch configuration")
        require(case.get("dispatch_groups") == [1,1,1] and case.get("workgroup_size") == [64,1,1] and case.get("root_bytes") == 16, "wrong root or group ABI")
        require(case.get("bytes") == 1024 and case.get("selected_bytes") == 256 and case.get("unchanged_sentinel_bytes") == 768, "wrong checked byte ranges")
        actual = outputs.get((level, slot), b"")
        expected = expected_bytes(slot, golden)
        require(len(golden) == 256 and actual == expected, f"opt{level} slot{slot}: selected golden or untouched descriptor mismatch")
        require(case.get("actual_sha256") == sha256(actual) and case.get("golden_sha256") == sha256(golden), "output digest mismatch")
        require(case.get("payload_sha256") == artifacts[index // 2]["payload_sha256"], "wrong shader payload")
        # One extra main-device copy initializes the size control for each opt
        # level. Its rejection then isolates size, not uninitialized contents.
        require(case.get("completion_value") == 6 + 9 * index + index // 2 and case.get("readback_completion_value") == 10 + 9 * index + index // 2, "missing or unordered completion")
    controls = record.get("controls", [])
    require(len(controls) == 10 and {row.get("case_id") for row in controls} == CONTROLS and all(row.get("status") == "pass" for row in controls), "required native controls missing or failed")
    device = record.get("device", {})
    require(device.get("validation") is True and device.get("synchronization_validation") is True, "validation not enabled")
    require(min(device.get("api_version", 0), device.get("loader_api_version", 0)) >= (1 << 22 | 4 << 12) and set(device.get("enabled_features", [])) == FEATURES, "required API/features missing")


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
    token = str(uuid.uuid4())
    log = RunLog(output, token)
    executable, sdk, shaders = args.executable.resolve(strict=True), args.sdk.resolve(strict=True), args.shader_dir.resolve(strict=True)
    identity = {path: sha256((ROOT / path).read_bytes()) for path in SOURCES}
    report = {"schema_version": 1, "case_id": "agfx.compute_multi_dispatch_buffer.run", "status": "fail", "run_token": token,
              "required": 4, "executed": 0, "passed": 0, "host_source_sha256": identity,
              "executable_sha256": sha256(executable.read_bytes()),
              "validation_layer_sha256": sha256((sdk / "Bin/VkLayer_khronos_validation.dll").read_bytes())}
    for filename in ["result.json", "native.stdout.txt", "native.stderr.txt"] + [f"compute-opt{level}-slot{slot}.bin" for level, slot in CASES]:
        (output / filename).unlink(missing_ok=True)
    environment = os.environ.copy()
    for name in ("VK_LAYER_ENABLES", "VK_LAYER_DISABLES", "VK_LAYER_SETTINGS_PATH"):
        environment.pop(name, None)
    environment.update({"VK_LOADER_LAYERS_DISABLE": "~implicit~", "VK_LAYER_PATH": str(sdk / "Bin"), "VK_LOADER_DEBUG": "layer", "VK_LAYER_VALIDATE_CORE": "1", "VK_LAYER_VALIDATE_SYNC": "1"})
    environment["PATH"] = str(sdk / "Bin") + os.pathsep + environment["PATH"]
    command = [str(executable), "--run-token", token, "--shader-dir", str(shaders)]
    report.update(command=command, environment={k:v for k,v in environment.items() if k.startswith("VK_")})
    start = time.monotonic()
    try:
        compiled = subprocess.run([str(executable), "--identity"], capture_output=True, timeout=10, check=True)
        require(json.loads(compiled.stdout) == identity, "stale executable rejected before GPU execution")
        log.event("identity_checked")
        artifacts = []
        for level in (0,3):
            artifact = json.loads((shaders / f"compute_multi_dispatch_opt{level}.metadata.json").read_text(encoding="utf-8"))
            require(artifact["payload_sha256"] == sha256((shaders / f"compute_multi_dispatch_opt{level}.spv").read_bytes()), "stale payload metadata")
            require(artifact["identity"]["source_sha256"] == identity["shaders/rust/compute_multi_dispatch.rs"], "stale shader source")
            artifacts.append(artifact)
        report["shader_metadata"] = artifacts
        log.event("artifacts_checked")
        report["executed"] = None
        log.event("native_started", command=command)
        result = subprocess.run(command, cwd=output, env=environment, capture_output=True, timeout=45)
        log.event("native_finished", exit_code=result.returncode)
        (output / "native.stdout.txt").write_bytes(result.stdout)
        (output / "native.stderr.txt").write_bytes(result.stderr)
        report["exit_code"] = result.returncode
        out, err = result.stdout.decode("utf-8", "replace"), result.stderr.decode("utf-8", "replace")
        diagnostics, notices = validation_messages(out + err)
        inserted = 'Insert instance layer "VK_LAYER_KHRONOS_validation"' in err and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err
        report.update(validation_inserted=inserted, validation_diagnostics=diagnostics, intentional_loader_notices=notices)
        record = json.loads(out)
        if type(record.get("executed")) is int:
            report["executed"] = record["executed"]
        require(result.returncode == 0 and inserted and not diagnostics, "native exit or explicit validation failed")
        outputs = {(level, slot): (output / f"compute-opt{level}-slot{slot}.bin").read_bytes() for level,slot in CASES}
        golden = (ROOT / SOURCES[-1]).read_bytes()
        check_record(record, token, identity, artifacts, outputs, golden)
        report.update(status="pass", executed=4, passed=4, native=record)
        log.event("verified", cases=4)
    except subprocess.TimeoutExpired as error:
        (output / "native.stdout.txt").write_bytes(error.stdout or b"")
        (output / "native.stderr.txt").write_bytes(error.stderr or b"")
        report.update(failure="native timeout", timed_out=True)
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    except KeyboardInterrupt:
        report.update(status="incomplete", failure="interrupted")
    report["elapsed_seconds"] = time.monotonic() - start
    log.finish(report)
    print(json.dumps({k: report.get(k) for k in ("case_id", "status", "failure", "required", "executed", "passed", "validation_inserted", "elapsed_seconds")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    raise SystemExit(main())
