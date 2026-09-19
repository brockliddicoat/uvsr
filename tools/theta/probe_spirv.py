"""Probe installed SPIRV-Tools without claiming RustGPU or device coverage."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    fixtures = root / "tests/compiler-probes"
    args.output.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ""
    tools = {name: args.tools / ("spirv-" + name + suffix)
             for name in ("as", "val", "link", "opt", "dis")}
    records = []
    identities = {}
    for name, path in tools.items():
        if not path.is_file():
            parser.error(f"missing required tool: {path}")
        identities[name] = dict(path=str(path.resolve()), sha256=sha256(path))

    def command(record, stage, name, arguments, expected_error=None):
        argv = [str(tools[name].resolve()), *map(str, arguments)]
        stem = args.output / f"{record['case_id']}.{stage}"
        try:
            result = subprocess.run(argv, capture_output=True, timeout=45)
        except (OSError, subprocess.TimeoutExpired) as error:
            record["steps"].append(dict(stage=stage, command=argv, error=str(error)))
            raise RuntimeError(f"{stage}: {error}") from error
        stem.with_suffix(stem.suffix + ".stdout.txt").write_bytes(result.stdout)
        stem.with_suffix(stem.suffix + ".stderr.txt").write_bytes(result.stderr)
        record["steps"].append(dict(stage=stage, command=argv, exit_code=result.returncode))
        if expected_error is not None:
            if result.returncode == 0 or expected_error not in result.stderr.decode(errors="replace"):
                raise RuntimeError(f"{stage}: expected rejection containing {expected_error!r}")
        elif result.returncode != 0:
            raise RuntimeError(f"{stage}: exit {result.returncode}")
        return result.stdout

    expected = {
        "logical_store": ["OpMemoryModel Logical", "OpStore"],
        "physical_store": ["PhysicalStorageBuffer64", "OpConvertUToPtr", "Aligned 4"],
        "untyped_store": ["OpTypeUntypedPointerKHR", "OpUntypedAccessChainKHR", "OpStore"],
        "descriptor_heaps": ["ResourceHeapEXT", "SamplerHeapEXT", "OpConstantSizeOfEXT",
                             "ArrayStrideIdEXT", "OpUntypedAccessChainKHR",
                             "OpImageSampleExplicitLod", "PhysicalStorageBuffer64", "Aligned 16"],
    }
    for name, instructions in expected.items():
        source = fixtures / (name + ".spvasm")
        record = dict(case_id="theta.tools." + name, source=str(source.relative_to(root)),
                      source_sha256=sha256(source), status="fail", steps=[])
        records.append(record)
        binary = args.output / (name + ".spv")
        linked = args.output / (name + ".linked.spv")
        optimized = args.output / (name + ".optimized.spv")
        text = args.output / (name + ".roundtrip.spvasm")
        roundtrip = args.output / (name + ".roundtrip.spv")
        try:
            command(record, "assemble", "as", ["--target-env", "vulkan1.3", source, "-o", binary])
            command(record, "validate", "val", ["--target-env", "vulkan1.3", binary])
            command(record, "link", "link", ["--target-env", "vulkan1.3", binary, "-o", linked])
            command(record, "optimize", "opt", ["--target-env=vulkan1.3", "-O", linked, "-o", optimized])
            command(record, "validate_optimized", "val", ["--target-env", "vulkan1.3", optimized])
            command(record, "disassemble", "dis", [optimized, "-o", text])
            disassembly = text.read_text()
            for instruction in instructions:
                if instruction not in disassembly:
                    raise RuntimeError(f"missing required instruction after optimization: {instruction}")
            if name == "logical_store" and "PhysicalStorageBuffer" in disassembly:
                raise RuntimeError("logical-pointer control acquired physical addressing")
            if name == "descriptor_heaps" and any(x in disassembly for x in ("DescriptorSet", " Binding ")):
                raise RuntimeError("heap probe acquired an ordinary descriptor binding")
            command(record, "reassemble", "as", ["--target-env", "vulkan1.3", text, "-o", roundtrip])
            command(record, "validate_roundtrip", "val", ["--target-env", "vulkan1.3", roundtrip])
            if binary.stat().st_size < 20 or roundtrip.stat().st_size < 20:
                raise RuntimeError("missing or empty SPIR-V output")
            record.update(status="pass", output_sha256=sha256(roundtrip))
        except (RuntimeError, OSError) as error:
            record["first_failure"] = str(error)

    negatives = [
        ("missing_alignment", "physical_store", " Aligned 4", "", "Aligned"),
        ("missing_heap_capability", "descriptor_heaps", "OpCapability DescriptorHeapEXT\n", "", "DescriptorHeapEXT"),
    ]
    for name, base, old, new, diagnostic in negatives:
        source = fixtures / (base + ".spvasm")
        modified = args.output / (name + ".spvasm")
        binary = args.output / (name + ".spv")
        record = dict(case_id="theta.tools." + name, source_sha256=sha256(source), status="fail", steps=[])
        records.append(record)
        try:
            original = source.read_text()
            if original.count(old) != 1:
                raise RuntimeError("negative control mutation no longer matches exactly once")
            modified.write_text(original.replace(old, new))
            command(record, "assemble", "as", ["--target-env", "vulkan1.3", modified, "-o", binary])
            command(record, "reject", "val", ["--target-env", "vulkan1.3", binary], diagnostic)
            record["status"] = "pass"
        except (RuntimeError, OSError) as error:
            record["first_failure"] = str(error)

    passed = sum(row["status"] == "pass" for row in records)
    result = dict(schema_version=1, scope="installed SPIRV-Tools only", required=6,
                  executed=len(records), passed=passed, failed=len(records)-passed,
                  tools=identities, runner_sha256=sha256(Path(__file__)), cases=records,
                  pending=["RustGPU codegen/parser", "SPIR-T qptr", "RustGPU linker",
                           "compiled-tools configuration", "Rust-authored device execution"])
    (args.output / "results.json").write_text(json.dumps(result, indent=2))
    print(json.dumps({key: value for key, value in result.items() if key not in ("tools", "cases")}))
    for row in records:
        if row["status"] != "pass":
            print(json.dumps(row))
    return 0 if len(records) == 6 and passed == 6 else 1


if __name__ == "__main__":
    sys.exit(main())
