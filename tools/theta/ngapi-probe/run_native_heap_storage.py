"""Validate sixteen actual NGAPI storage-image cases and their complete readback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

from compile_native_heap_storage import CAPABILITIES, EXTENSIONS
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_log import RunLog

PREFIX = "theta.m2.ngapi.native_heap_storage."
VARIANTS = [f"{pipeline}_opt{opt}" for pipeline in ("default", "qptr") for opt in (0, 3)]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_words(phase):
    mask = 0xffffffff
    seed = 0x12345678 if phase < 2 else 0xfffffff0
    images = [[(seed + texture * 256 + word * 3) & mask for word in range(16)] for texture in range(2)]
    source = phase % 2
    values = [(images[source][4 + channel] + channel + 1) & mask for channel in range(4)]
    images[1 - source][8:12] = values
    return values + [0xa5c37e19] * 4 + images[0] + images[1]


def check_records(records, metadata):
    ids = {PREFIX + "controls"} | {f"{PREFIX}{variant}.phase{phase}" for variant in VARIANTS for phase in range(4)}
    require(len(records) == 17 and {row["case_id"] for row in records} == ids, "missing, duplicate or unexpected cases")
    for row in records:
        require(row["status"] == "pass", "required native case did not pass")
        if row["case_id"] == PREFIX + "controls":
            require(row["checks"] == 169 and row["shader_cases_executed"] == 0, "wrong native control count")
            continue
        variant, phase = row["case_id"][len(PREFIX):].split(".phase")
        phase = int(phase)
        address = int(row["output_address"], 16)
        require(0 < address <= 2**64 - 33 and address % 4 == 0, "invalid physical output range")
        require(row["source_sha256"] == metadata[variant]["identity"]["source_sha256"]
                and row["payload_sha256"] == metadata[variant]["payload_sha256"], "stale embedded shader")
        require(row["source_index"] == (1 if phase % 2 == 0 else 3)
                and row["destination_index"] == (3 if phase % 2 == 0 else 1)
                and 0 < row["image_descriptor_bytes"] <= (2**64 - 1) // 4 and row["heap_slots"] == 4,
                "incorrect descriptor selection or stride")
        require(row["actual"] == expected_words(phase) and row["first_mismatch_word"] == -1
                and row["shader_cases_executed"] == 1 and row["completion"] == phase + 1, "readback or completion mismatch")


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
        source = args.upstream.resolve() / "tests/compiletests/ui/descriptor_heap/auxiliary/storage_body.rs"
        identity = {"source_sha256": sha256(source), "host_sha256": sha256(Path(__file__).with_name("native_heap_storage.cpp"))}
        compiled = subprocess.run([str(exe), "--identity"], capture_output=True, timeout=10, check=True)
        metadata = {}
        for variant in VARIANTS:
            stem = args.shader_dir / ("native_heap_storage_" + variant)
            data = json.loads(stem.with_suffix(".metadata.json").read_text())
            require(data["schema_version"] == 1 and data["status"] == "pass" and data["variant"] == variant
                    and data["language"] == "Rust" and data["payload_type"] == "SPIR-V"
                    and data["stage"] == "compute" and data["entry_point"] == "computeMain"
                    and data["profile"] == "ngapi-native-heap-storage" and data["target"] == "spirv-unknown-vulkan1.3-physical64"
                    and data["root_bytes"] == 16 and data["output_bytes"] == 32
                    and data["texture_extent"] == [2, 2] and data["texture_format"] == "rgba32_uint"
                    and data["optimization_level"] == int(variant[-1])
                    and data["spirt_passes"] == (["qptr"] if variant.startswith("qptr") else [])
                    and data["workgroup_size"] == [1, 1, 1] and set(data["capabilities"]) == CAPABILITIES
                    and set(data["extensions"]) == EXTENSIONS and data["identity"]["source_sha256"] == identity["source_sha256"]
                    and data["payload_sha256"] == sha256(stem.with_suffix(".spv")), "shader metadata mismatch")
            metadata[variant] = data
        identity["payloads"] = {variant: data["payload_sha256"] for variant, data in metadata.items()}
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
            report["executed"] = reported if code is not None else None
        adapter = re.search(r'Using "([^"]+)" with driver: "([^"]+)"', err_text)
        report.update(cases=records, validation_inserted=inserted, validation_diagnostics=diagnostics,
                      adapter=adapter[1] if adapter else None, driver_path=adapter[2] if adapter else None,
                      stdout_sha256=hashlib.sha256(stdout).hexdigest(), stderr_sha256=hashlib.sha256(stderr).hexdigest())
        check_records(records, metadata)
        require(code == 0 and inserted and not diagnostics, "native exit or Vulkan validation failed")
        report.update(status="pass", passed=16,
                      nonzero_high_address_bits_covered=any(int(row.get("output_address", "0"), 16) >> 32 for row in records))
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    log.finish(report)
    print(json.dumps({key: report.get(key) for key in ("status", "required", "executed", "passed", "failure")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
