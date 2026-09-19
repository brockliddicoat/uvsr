"""Validate all twelve aggregate/alias cases from the actual NGAPI executable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_log import RunLog

PREFIX = "theta.m2.ngapi.physical_operations."
VARIANTS = [f"{pipeline}_opt{opt}" for pipeline in ("default", "qptr") for opt in (0, 3)]
SEEDS = [0, 0x12345678, 0xfffffffc]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_words(seed, destination):
    mask, guard = 0xffffffff, 0xa5c37e19
    original = [seed, seed ^ 0x55aa55aa, (seed + 19) & mask]
    changed = [(word + index + 1) & mask for index, word in enumerate(original)]
    address = [destination & mask, destination >> 32]
    tag = seed ^ 0xcedef00d
    return changed + [sum(changed) & mask] + address + [tag, guard] + address + original + [tag, guard, guard]


def check_records(records, metadata):
    ids = {PREFIX + "controls"} | {f"{PREFIX}{variant}.seed{index}" for variant in VARIANTS for index in range(3)}
    require(len(records) == 13 and {row["case_id"] for row in records} == ids, "missing, duplicate or unexpected cases")
    for row in records:
        require(row["status"] == "pass", "required native case did not pass")
        if row["case_id"] == PREFIX + "controls":
            require(row["checks"] == 25 and row["shader_cases_executed"] == 0, "wrong native control count")
            continue
        variant, seed_index = row["case_id"][len(PREFIX):].split(".seed")
        seed_index = int(seed_index)
        source, destination, alias = (int(row[key], 16) for key in ("source_address", "destination_address", "alias_address"))
        require(0 < source <= 2**64-33 and 0 < destination <= 2**64-33
                and source % 8 == 0 and destination % 8 == 0 and alias == destination
                and (source + 32 <= destination or destination + 32 <= source), "invalid allocation or alias range")
        require(row["source_sha256"] == metadata[variant]["identity"]["source_sha256"]
                and row["payload_sha256"] == metadata[variant]["payload_sha256"], "stale embedded shader")
        require(row["seed"] == SEEDS[seed_index] and row["actual"] == expected_words(SEEDS[seed_index], destination)
                and row["first_mismatch_word"] == -1 and row["shader_cases_executed"] == 1
                and row["completion"] == seed_index + 1
                and row["nonzero_high_address_bits"] == bool((source | destination) >> 32), "readback or completion mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("executable", "sdk", "shader-dir", "upstream", "output-dir"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").unlink(missing_ok=True)
    for name in ("stdout.txt", "stderr.txt"):
        (output / name).unlink(missing_ok=True)
    log = RunLog(output, str(uuid.uuid4()))
    report = {"schema_version": 1, "case_id": PREFIX + "run", "status": "fail", "required": 12, "executed": 0, "passed": 0}
    try:
        exe, sdk = args.executable.resolve(strict=True), args.sdk.resolve(strict=True)
        source = args.upstream.resolve() / "tests/compiletests/ui/physical_storage/auxiliary/operations_body.rs"
        identity = {"source_sha256": sha256(source), "host_sha256": sha256(Path(__file__).with_name("physical_operations.cpp")),
                    "root_sha256": sha256(Path(__file__).with_name("physical_readback.hpp"))}
        compiled = subprocess.run([str(exe), "--identity"], capture_output=True, timeout=10, check=True)
        metadata = {}
        for variant in VARIANTS:
            stem = args.shader_dir / ("physical_operations_" + variant)
            data = json.loads(stem.with_suffix(".metadata.json").read_text())
            require(data["status"] == "pass" and data["variant"] == variant and data["stage"] == "compute"
                    and data["entry_point"] == "computeMain" and data["profile"] == "ngapi-physical-operations"
                    and data["target"] == "spirv-unknown-vulkan1.3-physical64"
                    and data["root_bytes"] == 16 and data["allocation_bytes"] == 32 and data["source_bytes"] == 24
                    and data["optimization_level"] == int(variant[-1])
                    and data["spirt_passes"] == (["qptr"] if variant.startswith("qptr") else [])
                    and data["workgroup_size"] == [1, 1, 1] and data["identity"]["source_sha256"] == identity["source_sha256"]
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
                      environment={k: v for k, v in environment.items() if k.startswith("VK_")})
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
        report.update(status="pass", passed=12,
                      nonzero_high_address_bits_covered=any(row.get("nonzero_high_address_bits", False) for row in records))
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        report["failure"] = str(error)
    log.finish(report)
    print(json.dumps({key: report.get(key) for key in ("status", "required", "executed", "passed", "failure")}))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
