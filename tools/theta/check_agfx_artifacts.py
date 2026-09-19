"""Exercise invalid artifact rejections before the explicit fixture opens Vulkan."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import uuid
from run_agfx_copy import require, sha256
from run_log import RunLog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--shader-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").unlink(missing_ok=True)
    log = RunLog(output, str(uuid.uuid4()))
    exe, source = args.executable.resolve(strict=True), args.shader_dir.resolve(strict=True)
    require(not source.is_relative_to(output), "control output must not contain the source artifact directory")
    records = []
    for name in ("missing_payload", "changed_payload", "wrong_stage", "wrong_entry", "wrong_profile", "stale_source", "missing_feature", "incomplete_compile"):
        target = output / name
        target.mkdir(exist_ok=True)
        for level in (0,3):
            for suffix in ("spv", "metadata.json"):
                filename = f"compute_multi_dispatch_opt{level}.{suffix}"
                (target / filename).write_bytes((source / filename).read_bytes())
        payload, meta = target / "compute_multi_dispatch_opt0.spv", target / "compute_multi_dispatch_opt0.metadata.json"
        data = json.loads(meta.read_text(encoding="utf-8"))
        if name == "missing_payload": payload.unlink()
        elif name == "changed_payload":
            changed = bytearray(payload.read_bytes()); changed[-1] ^= 1; payload.write_bytes(changed)
        elif name == "wrong_stage": data["stage"] = "fragment"
        elif name == "wrong_entry": data["entry_point"] = "missing"
        elif name == "wrong_profile": data["profile"] = "ngapi-native-heap"
        elif name == "stale_source": data["identity"]["source_sha256"] = "0" * 64
        elif name == "missing_feature": data["capabilities"].remove("StorageBufferArrayDynamicIndexing")
        elif name == "incomplete_compile": data["status"] = "incomplete"
        meta.write_text(json.dumps(data), encoding="utf-8")
        environment = os.environ.copy(); environment["VK_LOADER_DEBUG"] = "all"
        result = subprocess.run([str(exe), "--run-token", name, "--shader-dir", str(target)], cwd=target, env=environment, capture_output=True, timeout=10)
        (target / "stdout.txt").write_bytes(result.stdout)
        (target / "stderr.txt").write_bytes(result.stderr)
        require(result.returncode != 0 and not result.stdout, f"{name}: invalid artifact accepted")
        require(b"Vulkan Loader" not in result.stderr and b"agfx.compute_multi_dispatch_buffer failed:" in result.stderr, f"{name}: unexpected failure/device activity")
        records.append({"case_id": "agfx.artifact.control." + name, "status": "pass", "exit_code": result.returncode, "diagnostic": result.stderr.decode("utf-8").strip()})
        log.event("control_verified", case_id=records[-1]["case_id"])
    report = {"schema_version": 1, "status": "pass", "required": 8, "executed": 8, "passed": 8, "executable_sha256": sha256(exe.read_bytes()), "cases": records}
    log.finish(report)
    print(json.dumps({"status": "pass", "artifact_controls": 8, "GPU_execution": False}))


if __name__ == "__main__": main()
