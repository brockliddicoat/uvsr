"""Compile the close AGFX raster shader translations with the pinned RustGPU."""
import argparse
import json
from pathlib import Path
import re

from shader_build import compiler_inputs, run, sha256

ENTRIES = {
    'raster_vs': ('vertex', 0), 'raster_fs': ('fragment', 32),
    'triangle_vs': ('vertex', 0), 'color3_fs': ('fragment', 0),
    'depth_vs': ('vertex', 32), 'depth_fullscreen_vs': ('vertex', 32),
    'blend_vs': ('vertex', 48), 'blend_fullscreen_vs': ('vertex', 48),
    'indexed_vs': ('vertex', 16), 'color4_fs': ('fragment', 0),
    'pass_vs': ('vertex', 32), 'pass_fs': ('fragment', 32),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustgpu-source', type=Path, required=True)
    parser.add_argument('--codegen-backend', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    upstream = args.rustgpu_source.resolve(strict=True)
    backend = args.codegen_backend.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[2] / 'shaders/rust/raster.rs'
    for level in (0, 3):
        (output / f'raster_opt{level}.metadata.json').unlink(missing_ok=True)
    common, identity, validator, disassembler = compiler_inputs(upstream, backend, source, 'spirv-unknown-vulkan1.3')
    for level in (0, 3):
        stem = f'raster_opt{level}'
        module = output / f'{stem}.spv'
        command = common + [f'-Copt-level={level}', '-o', str(module) + '.json']
        run(command, upstream, output / f'{stem}_rustc')
        manifest = json.loads(Path(str(module)+'.json').read_text(encoding='utf-8'))
        if sorted(manifest['entry_points']) != sorted(ENTRIES):
            raise RuntimeError('unexpected raster entry points')
        run([validator, '--target-env', 'vulkan1.3', str(module)], upstream, output / f'{stem}_validate')
        assembly = run([disassembler, str(module)], upstream, output / f'{stem}_disassemble')
        (output / f'{stem}.spvasm').write_text(assembly, encoding='utf-8')
        for entry, (stage, _) in ENTRIES.items():
            kind = 'Vertex' if stage == 'vertex' else 'Fragment'
            if not re.search(rf'OpEntryPoint {kind} %\S+ "{entry}"', assembly):
                raise RuntimeError(f'wrong stage for {entry}')
        for pattern in [r'OpMemoryModel Logical Vulkan', r'BuiltIn PointSize', r'\bOpKill\b',
                        r'OpDecorate %\S+ DescriptorSet 0', r'OpDecorate %\S+ Binding 0',
                        r'OpDecorate %\S+ ArrayStride 32']:
            if not re.search(pattern, assembly):
                raise RuntimeError(f'missing raster contract {pattern}')
        caps = re.findall(r'OpCapability (\w+)', assembly)
        if set(caps) != {'Shader', 'VulkanMemoryModel'} or re.search(r'PhysicalStorageBuffer|ResourceHeapEXT|SamplerHeapEXT|OpTypeImage|OpTypeSampler', assembly):
            raise RuntimeError('unexpected raster capability or resource profile')
        record = dict(schema_version=1, case_id=f'agfx.raster.compile.opt{level}', status='pass',
            language='Rust', stage='vertex+fragment', entries=ENTRIES,
            target='spirv-unknown-vulkan1.3', profile='agfx-ordinary-raster', payload_type='SPIR-V',
            payload_sha256=sha256(module), identity=identity, rustc_command=command,
            capabilities=caps, extensions=re.findall(r'OpExtension "([^"]+)"', assembly))
        (output / f'{stem}.metadata.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({k: record[k] for k in ('case_id','status','payload_sha256','capabilities')}))


if __name__ == '__main__':
    main()
