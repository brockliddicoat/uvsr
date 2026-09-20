"""Compile the ordinary descriptor-array AGFX fixture with the pinned logical32 sysroot."""
import argparse
import json
from pathlib import Path
import re

from shader_build import compiler_inputs, run, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustgpu-source', type=Path, required=True)
    parser.add_argument('--codegen-backend', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--sampling', action='store_true', help='compile source texture seed and sampling entries')
    args = parser.parse_args()
    upstream = args.rustgpu_source.resolve(strict=True)
    backend = args.codegen_backend.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = 'texture_sampling' if args.sampling else 'compute_multi_dispatch'
    source = Path(__file__).resolve().parents[2] / f'shaders/rust/{name}.rs'
    for level in (0, 3):
        (output / f'{name}_opt{level}.metadata.json').unlink(missing_ok=True)
    common, identity, validator, disassembler = compiler_inputs(upstream, backend, source, 'spirv-unknown-vulkan1.3')
    if not args.sampling:
        common += ['-Ctarget-feature=+RuntimeDescriptorArray,+StorageBufferArrayDynamicIndexing,+ext:SPV_EXT_descriptor_indexing']
    for level in (0, 3):
        stem = f'{name}_opt{level}'
        module = output / f'{stem}.spv'
        command = common + [f'-Copt-level={level}', '-o', str(module) + '.json']
        run(command, upstream, output / f'{stem}_rustc')
        manifest = json.loads(Path(str(module)+'.json').read_text(encoding='utf-8'))
        entries = ['sample_cs', 'seed_cs'] if args.sampling else ['main_cs']
        if sorted(manifest['entry_points']) != entries:
            raise RuntimeError('unexpected AGFX entry point')
        run([validator, '--target-env', 'vulkan1.3', str(module)], upstream, output / f'{stem}_validate')
        assembly = run([disassembler, str(module)], upstream, output / f'{stem}_disassemble')
        (output / f'{stem}.spvasm').write_text(assembly, encoding='utf-8')
        patterns = ([r'OpMemoryModel Logical Vulkan', r'OpEntryPoint GLCompute %\S+ "sample_cs"',
                     r'OpEntryPoint GLCompute %\S+ "seed_cs"', r'OpExecutionMode %\S+ LocalSize 8 8 1',
                     r'OpImageSampleExplicitLod', r'OpTypeSampler', r'OpTypeImage %\S+ 2D 2 0 0 1 Unknown']
                    if args.sampling else [r'OpMemoryModel Logical Vulkan', r'OpEntryPoint GLCompute %\S+ "main_cs"',
                        r'OpExecutionMode %\S+ LocalSize 64 1 1', r'OpDecorate %\S+ DescriptorSet 0',
                        r'OpDecorate %\S+ Binding 0', r'OpCapability RuntimeDescriptorArray'])
        for pattern in patterns:
            if not re.search(pattern, assembly):
                raise RuntimeError(f'AGFX module lacks {pattern}')
        caps = {'Shader', 'VulkanMemoryModel'}
        if not args.sampling:
            caps |= {'RuntimeDescriptorArray', 'StorageBufferArrayDynamicIndexing'}
        if set(re.findall(r'OpCapability (\w+)', assembly)) != caps:
            raise RuntimeError('AGFX capability requirements changed')
        if re.search(r'PhysicalStorageBuffer|ResourceHeapEXT|SamplerHeapEXT', assembly):
            raise RuntimeError('ordinary AGFX shader unexpectedly uses a physical/native-heap profile')
        root = re.search(r'(%\S+) = OpTypeStruct (%\S+) \2 \2 \2', assembly)
        if not root or any(f'OpMemberDecorate {root[1]} {i} Offset {i*4}' not in assembly for i in range(4)):
            raise RuntimeError('AGFX root layout changed')
        case = 'texture_sampling' if args.sampling else 'compute_multi_dispatch_buffer'
        record = dict(schema_version=1, case_id=f'agfx.{case}.compile.opt{level}', status='pass',
                      language='Rust', stage='compute', entry_points=entries, entry_point=None if args.sampling else 'main_cs',
                      workgroup_size=[8,8,1] if args.sampling else [64,1,1], root_bytes=[16,48] if args.sampling else 16,
                      target='spirv-unknown-vulkan1.3', profile='agfx-ordinary-sampled-image' if args.sampling else 'agfx-ordinary-storage-buffer-array', payload_type='SPIR-V',
                      payload_sha256=sha256(module), identity=identity, rustc_command=command,
                      capabilities=re.findall(r'OpCapability (\w+)', assembly),
                      extensions=re.findall(r'OpExtension "([^"]+)"', assembly))
        (output / f'{stem}.metadata.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({k: record[k] for k in ['case_id','status','payload_sha256','capabilities']}))


if __name__ == '__main__':
    main()
