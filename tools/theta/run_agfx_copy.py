"""Run the direct Rust Vulkan copy case with explicit validation and fresh identity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid
from run_log import RunLog

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["Cargo.toml", "Cargo.lock", "crates/agfx/Cargo.toml", "crates/agfx/src/lib.rs",
           "crates/agfx/src/vulkan.rs", "crates/agfx/src/vulkan/compute.rs", "crates/agfx/src/bin/buffer_copy.rs",
           "tests/parity/fixtures/agfx/copy_buffer_to_buffer.bin"]
CONTROLS = {"agfx.control." + name for name in (
    "zero_size", "unaligned_size", "unrepresentable_size", "uninitialized_read", "partial_write",
    "device_write", "readback_write", "upload_read", "uninitialized_source", "incomplete_destination",
    "overrun", "overlapping_destination", "empty_regions", "unaligned_region", "foreign_destination",
    "foreign_source", "foreign_completion")}
FEATURES = {"timelineSemaphore", "vulkanMemoryModel", "runtimeDescriptorArray", "shaderStorageBufferArrayDynamicIndexing"}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validation_messages(text):
    messages = re.findall(r"[^\r\n]*(?:Validation (?:Error|Warning)|AGFX validation:|VUID-|SYNC-HAZARD|ERROR \|)[^\r\n]*", text)
    # The loader forwards its intentional implicit-layer filter notices through
    # the instance's GENERAL callback. Retain them separately, matching the full
    # known text. Every other callback warning/error still fails the run.
    notice = re.compile(r'''AGFX validation: Layer "[A-Za-z0-9_]+" forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'\.''')
    return ([line for line in messages if not notice.fullmatch(line)],
            [line for line in messages if notice.fullmatch(line)])


def check_record(record, token, identity, actual, golden):
    require(record.get("schema_version") == 1 and record.get("run_token") == token, "version or fresh run token mismatch")
    require(record.get("status") == "pass" and record.get("host_source_sha256") == identity, "native status or compiled identity mismatch")
    require(all(type(record.get(key)) is int and record[key] == 1 for key in ("required", "executed", "passed")), "required case denominator mismatch")
    cases = record.get("cases", [])
    require(len(cases) == 1 and cases[0].get("case_id") == "agfx.copy_buffer_to_buffer" and cases[0].get("status") == "pass", "missing, duplicate or skipped copy case")
    case = cases[0]
    require(len(actual) == len(golden) == 256 and actual == golden, "missing, short or incorrect raw readback")
    require(case.get("bytes") == 256 and case.get("expected_sha256") == case.get("actual_sha256") == sha256(golden), "reported readback identity mismatch")
    require(case.get("completion_values") == [1, 2, 3], "ordered completion values missing")
    controls = record.get("controls", [])
    require(len(controls) == len(CONTROLS) and {row.get("case_id") for row in controls} == CONTROLS and all(row.get("status") == "pass" for row in controls), "required native controls missing or failed")
    device = record.get("device", {})
    require(device.get("validation") is True and device.get("synchronization_validation") is True, "validation not enabled")
    require(min(device.get("api_version", 0), device.get("loader_api_version", 0)) >= (1 << 22 | 4 << 12) and set(device.get("enabled_features", [])) == FEATURES, "required API/features missing")
    flags = record.get("memory_flags", {})
    require(flags.get("upload", 0) & 6 == 6 and flags.get("readback", 0) & 6 == 6 and flags.get("device", 0) & 1 == 1, "memory role flags missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").unlink(missing_ok=True)
    token = str(uuid.uuid4())
    log = RunLog(output, token)
    executable, sdk = args.executable.resolve(strict=True), args.sdk.resolve(strict=True)
    identity = {path: sha256((ROOT / path).read_bytes()) for path in SOURCES}
    report = {"schema_version": 1, "case_id": "agfx.copy_buffer_to_buffer.run", "status": "fail",
              "run_token": token, "executable_sha256": sha256(executable.read_bytes()), "host_source_sha256": identity,
              "validation_layer_sha256": sha256((sdk / "Bin/VkLayer_khronos_validation.dll").read_bytes()),
              "required": 1, "executed": 0, "passed": 0}
    for filename in ("result.json", "native.stdout.txt", "native.stderr.txt", "copy-buffer.bin"):
        (output / filename).unlink(missing_ok=True)
    environment = os.environ.copy()
    for name in ("VK_LAYER_ENABLES", "VK_LAYER_DISABLES", "VK_LAYER_SETTINGS_PATH"):
        environment.pop(name, None)
    environment.update({"VK_LOADER_LAYERS_DISABLE": "~implicit~", "VK_LAYER_PATH": str(sdk / "Bin"),
                        "VK_LOADER_DEBUG": "layer", "VK_LAYER_VALIDATE_CORE": "1", "VK_LAYER_VALIDATE_SYNC": "1"})
    environment["PATH"] = str(sdk / "Bin") + os.pathsep + environment["PATH"]
    command = [str(executable), "--run-token", token]
    report.update(command=command, environment={k: v for k, v in environment.items() if k.startswith("VK_")})
    start = time.monotonic()
    try:
        # This mode never opens Vulkan. Reject an old build before any GPU work.
        compiled = subprocess.run([str(executable), "--identity"], capture_output=True, timeout=10, check=True)
        require(json.loads(compiled.stdout) == identity, "stale executable rejected before GPU execution")
        log.event("identity_checked")
        report["executed"] = None  # A killed process may have executed unknown work.
        log.event("native_started", command=command)
        result = subprocess.run(command, cwd=output, env=environment, capture_output=True, timeout=45)
        log.event("native_finished", exit_code=result.returncode)
        (output / "native.stdout.txt").write_bytes(result.stdout)
        (output / "native.stderr.txt").write_bytes(result.stderr)
        report["exit_code"] = result.returncode
        out, err = result.stdout.decode("utf-8", "replace"), result.stderr.decode("utf-8", "replace")
        diagnostics, loader_notices = validation_messages(out + err)
        inserted = 'Insert instance layer "VK_LAYER_KHRONOS_validation"' in err and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err
        report.update(validation_inserted=inserted, validation_diagnostics=diagnostics, intentional_loader_notices=loader_notices)
        record = json.loads(out)
        if type(record.get("executed")) is int:
            report["executed"] = record["executed"]
        require(result.returncode == 0 and inserted and not diagnostics, "native exit or explicit validation failed")
        actual = (output / "copy-buffer.bin").read_bytes()
        golden = (ROOT / SOURCES[-1]).read_bytes()
        check_record(record, token, identity, actual, golden)
        report.update(status="pass", executed=1, passed=1, native=record, raw_sha256=sha256(actual))
        log.event("verified", cases=1)
    except subprocess.TimeoutExpired as error:
        (output / "native.stdout.txt").write_bytes(error.stdout or b"")
        (output / "native.stderr.txt").write_bytes(error.stderr or b"")
        report.update(failure="native timeout", timed_out=True)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    except KeyboardInterrupt:
        report.update(status="incomplete", failure="interrupted")
    report["elapsed_seconds"] = time.monotonic() - start
    log.finish(report)
    print(json.dumps({k: report.get(k) for k in ("case_id", "status", "failure", "required", "executed", "passed", "validation_inserted", "elapsed_seconds")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    raise SystemExit(main())
