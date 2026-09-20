"""Check source texture goldens and exact transfer controls. Requires Pillow."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid

from PIL import Image
from run_agfx_copy import ROOT, require, sha256, validation_messages
from run_log import RunLog

SOURCES = ["Cargo.toml", "Cargo.lock", "crates/agfx/Cargo.toml", "crates/agfx/src/lib.rs",
           "crates/agfx/src/vulkan.rs", "crates/agfx/src/vulkan/compute.rs",
           "crates/agfx/src/vulkan/ownership.rs", "crates/agfx/src/vulkan/texture.rs",
           "crates/agfx/src/bin/texture_copy.rs", "tests/parity/fixtures/agfx/copy_buffer_to_texture.png",
           "tests/parity/fixtures/agfx/copy_texture_to_buffer.bin"]
COMPLETIONS = [[1, 2, 3], [4, 5, 6, 7], [8, 9], [10, 11], [12, 13], [14, 15, 16]]
CONTROLS = {"agfx.texture.control." + name for name in (
    "zero_extent", "size_overflow", "uninitialized_image", "uninitialized_upload", "partial_initialization",
    "overrun", "bad_pitch", "uninitialized_prefix", "foreign_upload", "foreign_image",
    "float_offset_alignment", "uninitialized_row_padding")}


def expected_outputs():
    manifest = json.loads((ROOT / "tests/parity/texture-sources.json").read_text())
    for golden in manifest["goldens"]:
        require(sha256((ROOT / golden["path"]).read_bytes()) == golden["sha256"], "changed frozen texture golden")
    with Image.open(ROOT / SOURCES[-2]) as image:
        require(image.size == (128, 128) and image.mode == "RGBA", "wrong original texture image layout")
        upload = image.tobytes()
    padded = bytearray([0xcd] * 104)
    for y in range(3):
        padded[12 + y * 32:32 + y * 32] = bytes(range(1 + y * 20, 21 + y * 20))
    return {"agfx.copy_buffer_to_texture": upload,
            "agfx.copy_texture_to_buffer": (ROOT / SOURCES[-1]).read_bytes(),
            "agfx.texture.clear_unorm": bytes([255, 0, 255, 255]) * 35,
            "agfx.texture.float_roundtrip": b"".join(struct.pack("<f", (i - 70) / 8) for i in range(140)),
            "agfx.texture.clear_float": struct.pack("<4f", -2, .25, 8, 1) * 35,
            "agfx.texture.padded_rows": bytes(padded)}


def check_record(record, token, identity, outputs):
    require(record.get("schema_version") == 1 and record.get("run_token") == token and record.get("status") == "pass", "version/token/status mismatch")
    require(record.get("host_source_sha256") == identity, "stale compiled texture source")
    require(all(type(record.get(k)) is int and record[k] == 6 for k in ("required", "executed", "passed")), "texture denominator mismatch")
    expected = expected_outputs()
    cases = record.get("cases", [])
    require(len(cases) == 6 and [case.get("case_id") for case in cases] == list(expected), "missing, duplicate or reordered texture cases")
    for case, (name, golden), completions in zip(cases, expected.items(), COMPLETIONS):
        actual = outputs.get(name, b"")
        require(case.get("status") == "pass" and actual == golden, f"{name}: source golden or exact control mismatch")
        require(case.get("bytes") == len(golden) and case.get("actual_sha256") == case.get("expected_sha256") == sha256(actual), "texture byte identity mismatch")
        require(case.get("completion_values") == completions, "texture completion order mismatch")
    controls = record.get("controls", [])
    require(len(controls) == 12 and {row.get("case_id") for row in controls} == CONTROLS and
            all(row.get("status") == "pass" for row in controls), "required texture controls missing or failed")
    device = record.get("device", {})
    require(device.get("validation") is True and device.get("synchronization_validation") is True, "validation disabled")
    require(min(device.get("api_version", 0), device.get("loader_api_version", 0)) >= (1 << 22 | 4 << 12), "wrong Vulkan device API")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id="agfx.texture.run", run_token=token, status="fail", required=6, executed=0, passed=0)
    try:
        exe, sdk = args.executable.resolve(strict=True), args.sdk.resolve(strict=True)
        identity = {path: sha256((ROOT / path).read_bytes()) for path in SOURCES}
        expected = expected_outputs()
        for name in expected:
            (output / f"{name}.bin").unlink(missing_ok=True)
        compiled = subprocess.run([str(exe), "--identity"], capture_output=True, check=True, timeout=10)
        require(json.loads(compiled.stdout) == identity, "stale executable rejected before Vulkan")
        report.update(host_source_sha256=identity, executable_sha256=sha256(exe.read_bytes()),
                      validation_layer_sha256=sha256((sdk / "Bin/VkLayer_khronos_validation.dll").read_bytes()))
        environment = os.environ.copy()
        for name in ("VK_LAYER_ENABLES", "VK_LAYER_DISABLES", "VK_LAYER_SETTINGS_PATH"):
            environment.pop(name, None)
        environment.update(VK_LOADER_LAYERS_DISABLE="~implicit~", VK_LAYER_PATH=str(sdk / "Bin"),
                           VK_LOADER_DEBUG="layer", VK_LAYER_VALIDATE_CORE="1", VK_LAYER_VALIDATE_SYNC="1")
        environment["PATH"] = str(sdk / "Bin") + os.pathsep + environment["PATH"]
        command = [str(exe), "--run-token", token]
        report.update(command=command, environment={k: v for k, v in environment.items() if k.startswith("VK_")}, executed=None)
        log.event("native_started", command=command)
        result = subprocess.run(command, cwd=output, env=environment, capture_output=True, timeout=45)
        (output / "native.stdout.txt").write_bytes(result.stdout)
        (output / "native.stderr.txt").write_bytes(result.stderr)
        out, err = result.stdout.decode("utf-8", "replace"), result.stderr.decode("utf-8", "replace")
        diagnostics, notices = validation_messages(out + err)
        inserted = ('Insert instance layer "VK_LAYER_KHRONOS_validation"' in err and
                    'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err)
        report.update(exit_code=result.returncode, validation_inserted=inserted, validation_diagnostics=diagnostics, intentional_loader_notices=notices)
        require(result.returncode == 0 and inserted and not diagnostics, "native exit or validation failed")
        record = json.loads(out)
        outputs = {name: (output / f"{name}.bin").read_bytes() for name in expected}
        check_record(record, token, identity, outputs)
        report.update(status="pass", executed=6, passed=6, native=record)
    except subprocess.TimeoutExpired as error:
        (output / "native.stdout.txt").write_bytes(error.stdout or b"")
        (output / "native.stderr.txt").write_bytes(error.stderr or b"")
        report.update(failure="native timeout", timed_out=True)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    except KeyboardInterrupt:
        report.update(status="incomplete", failure="interrupted")
    report["elapsed_seconds"] = time.monotonic() - start
    log.finish(report)
    print(json.dumps({k: report.get(k) for k in ("case_id", "status", "failure", "required", "executed", "passed", "validation_inserted", "elapsed_seconds")}))
    return report["status"] != "pass"


if __name__ == "__main__":
    raise SystemExit(main())
