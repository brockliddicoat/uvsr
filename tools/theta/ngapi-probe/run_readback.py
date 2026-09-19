"""Run the selected embedded Rust shader cases with explicit Vulkan validation.

This bounded Windows diagnostic checks case counts, identity, values and layer
insertion as well as the native exit status. A missing or unsupported run fails
the gate. It does not turn low device addresses into high-address coverage.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_records(records, metadata):
    prefix = "theta.m2.ngapi.physical_readback."
    expected_ids = {prefix + "controls"} | {f"{prefix}opt{opt}.seed{seed}" for opt in [0, 3] for seed in range(3)}
    if len(records) != 7 or {row["case_id"] for row in records} != expected_ids:
        raise ValueError("missing, duplicate or unexpected case IDs")
    if any(row["status"] != "pass" for row in records):
        raise ValueError("a required native case did not pass")
    for row in records:
        if row["case_id"].endswith("controls"):
            if row["checks"] != 16 or row["shader_cases_executed"] != 0:
                raise ValueError("CPU controls have an incorrect denominator")
            continue
        opt, seed_index = re.search(r"\.opt(0|3)\.seed([0-2])$", row["case_id"]).groups()
        expected = metadata[int(opt)]
        if (row["source_sha256"] != expected["identity"]["source_sha256"]
                or row["payload_sha256"] != expected["payload_sha256"]):
            raise ValueError("native executable contains a stale shader")
        address = int(row["source_address"], 16)
        seed = [0, 0x12345678, 0xfffffffc][int(seed_index)]
        words = [(seed + 7) & 0xffffffff, address & 0xffffffff, address >> 32,
                 0xa5c37e19, seed, 0xa5c37e19, 0xa5c37e19, 0xa5c37e19]
        if (row["seed"] != seed or row["actual"] != words or row["first_mismatch_word"] != -1
                or row["shader_cases_executed"] != 1 or row["nonzero_high_address_bits"] != bool(address >> 32)):
            raise ValueError(f"readback record mismatch: {row['case_id']}")


def check_heap_records(records, metadata):
    prefix = "theta.m2.ngapi.native_heap_sample."
    ids = {prefix + "controls"} | {f"{prefix}opt{opt}.resource{image}.sampler{sampler}"
                                 for opt in [0, 3] for image in [1, 3] for sampler in [2, 3]}
    if len(records) != 9 or {row["case_id"] for row in records} != ids:
        raise ValueError("missing, duplicate or unexpected heap case IDs")
    colors = [[2, 0, 0, 2], [0, 2, 0, 2], [0, 0, 2, 2], [2, 2, 2, 2]]
    pixels = [0xff0000ff, 0xff00ff00, 0xff000000, 0xff00ffff, 0xffff0000, 0xffffffff, 0xff000000, 0xffff00ff]
    strides = set()
    for row in records:
        if row["status"] != "pass":
            raise ValueError("a required native heap case did not pass")
        if row["case_id"] == prefix + "controls":
            if row["checks"] != 74 or row["shader_cases_executed"] != 0:
                raise ValueError("heap CPU controls have an incorrect denominator")
            continue
        opt, image, sampler = map(int, re.search(r"\.opt(0|3)\.resource([13])\.sampler([23])$", row["case_id"]).groups())
        expected = metadata[opt]
        address = int(row["output_address"], 16)
        color = colors[(2 if image == 3 else 0) + sampler - 2]
        words = [0x40000000 if value == 2 else 0 for value in color] + [0xa5c37e19] * 4 + pixels
        if (row["source_sha256"] != expected["identity"]["source_sha256"] or row["payload_sha256"] != expected["payload_sha256"]
                or row["resource_index"] != image or row["sampler_index"] != sampler
                or row["heap_slots"] != 4 or row["root_bytes"] != 16
                or row["image_descriptor_bytes"] <= 0 or row["sampler_descriptor_bytes"] <= 0
                or not 0 < address <= (1 << 64) - 33 or address % 16
                or row["nonzero_high_address_bits"] != bool(address >> 32)
                or row["actual"] != words or row["first_mismatch_word"] != -1 or row["shader_cases_executed"] != 1):
            raise ValueError(f"heap readback record mismatch: {row['case_id']}")
        strides.add((row["image_descriptor_bytes"], row["sampler_descriptor_bytes"]))
    if len(strides) != 1:
        raise ValueError("native descriptor sizes changed within the run")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--shader-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--fixture", choices=["physical_readback", "native_heap_sample"], default="physical_readback")
    args = parser.parse_args()
    fixture = args.fixture
    executable = args.executable.resolve(strict=True)
    sdk = args.sdk.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name(f"{fixture}.rs")
    metadata = {}
    for level in [0, 3]:
        stem = args.shader_dir / f"{fixture}_opt{level}"
        record = json.loads(stem.with_suffix(".metadata.json").read_text())
        if (record["status"] != "pass" or record["identity"]["source_sha256"] != sha256(source)
                or record["payload_sha256"] != sha256(stem.with_suffix(".spv"))):
            raise ValueError("shader metadata does not match the current source and compiled bytes")
        metadata[level] = record
    environment = os.environ.copy()
    environment.pop("VK_LAYER_ENABLES", None)
    environment.pop("VK_LAYER_DISABLES", None)
    environment.update({
        "VK_LOADER_LAYERS_DISABLE": "~implicit~",
        "VK_LAYER_PATH": str(sdk / "Bin"),
        "VK_INSTANCE_LAYERS": "VK_LAYER_KHRONOS_validation",
        "VK_LOADER_DEBUG": "layer",
        "VK_LAYER_VALIDATE_CORE": "1",
        "VK_LAYER_VALIDATE_SYNC": "1",
    })
    start = time.monotonic()
    timed_out = False
    try:
        result = subprocess.run([str(executable)], env=environment, capture_output=True, timeout=45)
        stdout, stderr, exit_code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr, exit_code = error.stdout or b"", error.stderr or b"", None
        timed_out = True
    (output / "stdout.txt").write_bytes(stdout)
    (output / "stderr.txt").write_bytes(stderr)
    out_text, err_text = stdout.decode(errors="replace"), stderr.decode(errors="replace")
    inserted = ('Insert instance layer "VK_LAYER_KHRONOS_validation"' in err_text
                and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err_text)
    diagnostics = re.findall(r"[^\r\n]*(?:Validation (?:Error|Warning)|NoGraphicsAPI validation:|VUID-|SYNC-HAZARD|ERROR \|)[^\r\n]*", out_text + err_text)
    records = []
    failure = None
    try:
        records = [json.loads(line) for line in out_text.splitlines() if line.strip()]
        (check_heap_records if fixture == "native_heap_sample" else check_records)(records, metadata)
        if exit_code != 0 or timed_out or not inserted or diagnostics:
            raise ValueError("native exit, validation insertion or validation diagnostics failed")
    except (ValueError, KeyError, TypeError) as error:
        failure = str(error)
    adapter = re.search(r'Using "([^"]+)" with driver: "([^"]+)"', err_text)
    report = {
        "schema_version": 1, "case_id": f"theta.m2.ngapi.{fixture}",
        "status": "pass" if failure is None else "fail", "failure": failure,
        "elapsed_seconds": time.monotonic() - start, "exit_code": exit_code, "timed_out": timed_out,
        "executable": str(executable), "executable_sha256": sha256(executable),
        "source_sha256": sha256(source), "required_shader_cases": 8 if fixture == "native_heap_sample" else 6,
        "shader_cases_executed": sum(row.get("shader_cases_executed", 0) for row in records if isinstance(row, dict)),
        "nonzero_high_address_bits_covered": any(row.get("nonzero_high_address_bits", False) for row in records if isinstance(row, dict)),
        "validation_inserted": inserted, "validation_diagnostics": diagnostics,
        "validation_layer_sha256": sha256(sdk / "Bin/VkLayer_khronos_validation.dll"),
        "environment": {key: environment[key] for key in environment if key.startswith("VK_")},
        "adapter": adapter[1] if adapter else None, "driver_path": adapter[2] if adapter else None,
        "stdout_sha256": hashlib.sha256(stdout).hexdigest(), "stderr_sha256": hashlib.sha256(stderr).hexdigest(),
        "cases": records,
    }
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ["case_id", "status", "failure", "shader_cases_executed", "validation_inserted", "nonzero_high_address_bits_covered"]}))
    return 0 if failure is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
