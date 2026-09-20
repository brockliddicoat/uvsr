"""Compile the bounded NGAPI consumer with the prepared physical64 test sysroot.

Run the prerequisite RustGPU physical_storage compiletests first. This diagnostic
uses their exact ABI-specific libraries, not the native host sysroot. Generated
SPIR-V, C++ includes, metadata and logs belong in an ignored output directory.
"""

import argparse
import json
from pathlib import Path
import re
import struct
import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from shader_build import compiler_inputs, run, sha256


def check_heap_profile(assembly, divergent=False):
    required = {
        r"BuiltIn ResourceHeapEXT": 1, r"BuiltIn SamplerHeapEXT": 1,
        r" = OpUntypedVariableKHR ": 2, r" = OpConstantSizeOfEXT ": 2,
        r"OpDecorateId %\S+ ArrayStrideIdEXT %\S+": 2,
        r" = OpUntypedAccessChainKHR ": 2, r" = OpImageSampleExplicitLod ": 1,
        r" = OpVectorTimesScalar ": 1, r"OpStore .* Aligned 16": 1,
    }
    if any(len(re.findall(pattern, assembly)) != count for pattern, count in required.items()):
        raise RuntimeError("module no longer matches the reviewed native heap sample profile")
    if divergent and ("BuiltIn LocalInvocationId" not in assembly
                      or len(re.findall(r" = OpBitwiseXor ", assembly)) != 2
                      or len(re.findall(r" = OpBitwiseAnd ", assembly)) != 2
                      or len(re.findall(r" = OpUConvert ", assembly)) != 1
                      or " = OpShiftRightLogical " not in assembly):
        raise RuntimeError("module no longer has the reviewed lane-dependent heap indices and output offset")
    root = re.search(r"(%\S+) = OpTypeStruct (%\S+) (%\S+) \3", assembly)
    if (not root or not re.search(re.escape(root[2]) + r" = OpTypeInt 64 0", assembly)
            or not re.search(re.escape(root[3]) + r" = OpTypeInt 32 0", assembly)
            or any(f"OpMemberDecorate {root[1]} {index} Offset {offset}" not in assembly
                   for index, offset in enumerate([0, 8, 12]))):
        raise RuntimeError("heap sample root no longer has the reviewed u64/u32/u32 layout")


def check_cube_profile(assembly, mesh=False):
    stages = ([r'OpEntryPoint TaskEXT %\S+ "taskMain"', r'OpEntryPoint MeshEXT %\S+ "meshMain"',
               r"OpEmitMeshTasksEXT", r"OpSetMeshOutputsEXT", r"OutputVertices 4\b", r"OutputPrimitivesEXT 2\b",
               r"OutputTrianglesEXT", r"BuiltIn WorkgroupId", r"BuiltIn PrimitiveTriangleIndicesEXT",
               r"OpVariable %\S+ TaskPayloadWorkgroupEXT"]
              if mesh else [r'OpEntryPoint Vertex %\S+ "vertexMain"', r"BuiltIn VertexIndex"])
    for pattern in stages + [r'OpEntryPoint Fragment %\S+ "fragmentMain"', r"BuiltIn Position",
                             r"OpImageSampleImplicitLod", r"OpLoad .* Aligned 4",
                             r"BuiltIn ResourceHeapEXT", r"BuiltIn SamplerHeapEXT", r"OpUntypedAccessChainKHR"]:
        if not re.search(pattern, assembly):
            raise RuntimeError(f"cube profile lacks {pattern}")
    root = re.search(r"(%\S+) = OpTypeStruct (%\S+) (%\S+) (%\S+) \4", assembly)
    if (not root or not re.search(re.escape(root[2]) + r" = OpTypeInt 64 0", assembly)
            or not re.search(re.escape(root[4]) + r" = OpTypeInt 32 0", assembly)
            or any(f"OpMemberDecorate {root[1]} {index} Offset {offset}" not in assembly
                   for index, offset in enumerate([0, 8, 72, 76]))):
        raise RuntimeError("cube root no longer has the reviewed 80-byte layout")
    if (len(re.findall(r" = OpConstantSizeOfEXT ", assembly)) != 2
            or len(re.findall(r"OpDecorateId .* ArrayStrideIdEXT", assembly)) != 2
            or len(re.findall(r" = OpUntypedVariableKHR ", assembly)) != 2):
        raise RuntimeError("cube native descriptor declarations changed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rustgpu-source", type=Path, required=True)
    parser.add_argument("--codegen-backend", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--fixture", choices=["physical_readback", "native_heap_sample", "native_heap_divergent", "native_heap_cube"], default="physical_readback")
    args = parser.parse_args()
    fixture = args.fixture
    heap = fixture != "physical_readback"
    divergent = fixture == "native_heap_divergent"
    cube = fixture == "native_heap_cube"
    lanes = 4 if divergent else 1
    upstream = args.rustgpu_source.resolve()
    backend = args.codegen_backend.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # A failed rebuild must not leave a compilable include from an older source.
    (output / f"{fixture}_modules.hpp").unlink(missing_ok=True)
    source = Path(__file__).with_name(f"{fixture}.rs").resolve()
    common, identity, validator, disassembler = compiler_inputs(upstream, backend, source, "spirv-unknown-vulkan1.3-physical64")
    if heap:
        features = "+UntypedPointersKHR,+DescriptorHeapEXT,+ext:SPV_KHR_untyped_pointers,+ext:SPV_EXT_descriptor_heap"
        if divergent:
            features += ",+ShaderNonUniform,+SampledImageArrayNonUniformIndexing"
        common += [f"-Ctarget-feature={features}"]
    header = ["// Generated by compile_readback.py. Do not edit or track.", "#pragma once", ""]
    for level in [0, 3]:
        stem = f"{fixture}_opt{level}"
        module = output / f"{stem}.spv"
        command = common + [f"-Copt-level={level}", "-o", str(module) + ".json"]
        run(command, upstream, output / f"{stem}_rustc")
        result = json.loads(Path(str(module) + ".json").read_text())
        if sorted(result["entry_points"]) != (["fragmentMain", "vertexMain"] if cube else ["computeMain"]):
            raise RuntimeError(f"unexpected entry points: {result['entry_points']}")
        run([validator, "--target-env", "vulkan1.3", str(module)], upstream, output / f"{stem}_validate")
        assembly = run([disassembler, str(module)], upstream, output / f"{stem}_disassemble")
        (output / f"{stem}.spvasm").write_text(assembly, encoding="utf-8")
        capabilities = [line.split("OpCapability ", 1)[1].strip() for line in assembly.splitlines() if "OpCapability " in line]
        extensions = [line.split("OpExtension ", 1)[1].strip().strip('"') for line in assembly.splitlines() if "OpExtension " in line]
        required_capabilities = {"Shader", "Int64", "VulkanMemoryModel", "PhysicalStorageBufferAddresses"}
        required_extensions = set()
        if heap:
            required_capabilities |= {"UntypedPointersKHR", "DescriptorHeapEXT"}
            required_extensions |= {"SPV_KHR_untyped_pointers", "SPV_EXT_descriptor_heap"}
        if divergent:
            required_capabilities |= {"ShaderNonUniform", "SampledImageArrayNonUniformIndexing"}
        if set(capabilities) != required_capabilities or set(extensions) != required_extensions:
            raise RuntimeError("module feature requirements changed. Review the NGAPI feature patch before dispatch")
        if ("OpMemoryModel PhysicalStorageBuffer64 Vulkan" not in assembly
                or re.search(r"\b(DescriptorSet|Binding|Uniform|StorageBuffer)\b", assembly)):
            raise RuntimeError("module no longer matches the reviewed physical addressing and descriptor profile")
        if not cube and (not re.search(r'OpEntryPoint GLCompute %\S+ "computeMain"', assembly)
                         or not re.search(rf"OpExecutionMode %\S+ LocalSize {lanes} 1 1", assembly)):
            raise RuntimeError("module no longer matches the reviewed compute entry")
        if cube:
            check_cube_profile(assembly)
        elif heap:
            check_heap_profile(assembly, divergent)
        else:
            if (len(re.findall(r"OpLoad .* Aligned 4", assembly)) != 1
                    or len(re.findall(r"OpStore .* Aligned 4", assembly)) != 3):
                raise RuntimeError("module no longer matches the reviewed scalar physical-readback profile")
            root_type = re.search(r"(%\S+) = OpTypeStruct (%\S+) \2", assembly)
            if (not root_type or not re.search(re.escape(root_type[2]) + r" = OpTypeInt 64 0", assembly)
                    or f"OpMemberDecorate {root_type[1]} 0 Offset 0" not in assembly
                    or f"OpMemberDecorate {root_type[1]} 1 Offset 8" not in assembly):
                raise RuntimeError("root data no longer has the reviewed two-u64 layout")
        record = {
            "schema_version": 1, "case_id": f"theta.m2.ngapi.{fixture}.compile.opt{level}",
            "status": "pass", "language": "Rust", "stage": "compute", "entry_point": "computeMain",
            "workgroup_size": [lanes, 1, 1], "target": "spirv-unknown-vulkan1.3-physical64",
            "profile": "ngapi-" + fixture.replace("_", "-"), "payload_type": "SPIR-V", "payload_sha256": sha256(module),
            "capabilities": capabilities, "extensions": extensions, "root_bytes": 16,
            "rustc_command": command, "identity": identity,
        }
        if cube:
            for key in ["stage", "entry_point", "workgroup_size"]:
                del record[key]
            record.update(root_bytes=80, entry_points=[{"stage": "vertex", "entry_point": "vertexMain"},
                                                     {"stage": "fragment", "entry_point": "fragmentMain"}])
        (output / f"{stem}.metadata.json").write_text(json.dumps(record, indent=2) + "\n")
        words = struct.unpack(f"<{module.stat().st_size // 4}I", module.read_bytes())
        header += [f'static const char {stem}_sha256[] = "{sha256(module)}";', f"static const gpu::uint32 {stem}[] = {{"]
        for start in range(0, len(words), 8):
            header += ["    " + ", ".join(f"0x{word:08x}" for word in words[start:start + 8]) + ","]
        header += ["};", ""]
        print(json.dumps({key: record[key] for key in ["case_id", "status", "payload_sha256", "capabilities"]}))
    header += [f'static const char {fixture}_source_sha256[] = "{identity["source_sha256"]}";', ""]
    (output / f"{fixture}_modules.hpp").write_text("\n".join(header), encoding="utf-8")


if __name__ == "__main__":
    main()
