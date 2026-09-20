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
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--views', action='store_true', help='compile the format-view compute/raster controls')
    mode.add_argument('--multisample', action='store_true', help='compile per-sample color/depth controls')
    args = parser.parse_args()
    upstream = args.rustgpu_source.resolve(strict=True)
    backend = args.codegen_backend.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = 'multisample' if args.multisample else 'texture_views' if args.views else 'raster'
    entries = ({'store_cs': ('compute',16), 'sample_cs': ('compute',0), 'load_cs': ('compute',0),
                'view_vs': ('vertex',0), 'view_fs': ('fragment',16)} if args.views else ENTRIES)
    if args.multisample:
        entries = {'samples_vs': ('vertex',48), 'samples_fs': ('fragment',48),
                   'read_ms_cs': ('compute',0), 'read_one_cs': ('compute',0)}
    source = Path(__file__).resolve().parents[2] / f'shaders/rust/{name}.rs'
    for level in (0, 3):
        (output / f'{name}_opt{level}.metadata.json').unlink(missing_ok=True)
    common, identity, validator, disassembler = compiler_inputs(upstream, backend, source, 'spirv-unknown-vulkan1.3')
    for level in (0, 3):
        stem = f'{name}_opt{level}'
        module = output / f'{stem}.spv'
        command = common + [f'-Copt-level={level}', '-o', str(module) + '.json']
        run(command, upstream, output / f'{stem}_rustc')
        manifest = json.loads(Path(str(module)+'.json').read_text(encoding='utf-8'))
        if sorted(manifest['entry_points']) != sorted(entries):
            raise RuntimeError('unexpected raster entry points')
        run([validator, '--target-env', 'vulkan1.3', str(module)], upstream, output / f'{stem}_validate')
        assembly = run([disassembler, str(module)], upstream, output / f'{stem}_disassemble')
        (output / f'{stem}.spvasm').write_text(assembly, encoding='utf-8')
        for entry, (stage, _) in entries.items():
            kind = {'vertex':'Vertex', 'fragment':'Fragment', 'compute':'GLCompute'}[stage]
            if not re.search(rf'OpEntryPoint {kind} %\S+ "{entry}"', assembly):
                raise RuntimeError(f'wrong stage for {entry}')
        patterns = ([r'OpMemoryModel Logical Vulkan', r'OpTypeImage %\S+ 2D 2 0 0 2 Rgba8',
                     r'\bOpImageRead\b', r'\bOpImageWrite\b', r'\bOpImageSampleExplicitLod\b'] if args.views else
                    [r'OpMemoryModel Logical Vulkan', r'BuiltIn PointSize', r'\bOpKill\b',
                        r'OpDecorate %\S+ DescriptorSet 0', r'OpDecorate %\S+ Binding 0',
                        r'OpDecorate %\S+ ArrayStride 32'])
        if args.multisample:
            patterns = [r'OpMemoryModel Logical Vulkan', r'BuiltIn SampleMask',
                        r'OpTypeImage %\S+ 2D 2 0 1 1 Unknown',
                        r'OpTypeImage %\S+ 2D 2 0 0 1 Unknown', r'OpImageFetch .+ Sample ',
                        r'OpMemberDecorate %\S+ 2 Offset 32']
        for pattern in patterns:
            if not re.search(pattern, assembly):
                raise RuntimeError(f'missing raster contract {pattern}')
        caps = re.findall(r'OpCapability (\w+)', assembly)
        forbidden = r'PhysicalStorageBuffer|ResourceHeapEXT|SamplerHeapEXT' + ('' if args.views or args.multisample else r'|OpTypeImage|OpTypeSampler')
        if set(caps) != {'Shader', 'VulkanMemoryModel'} or re.search(forbidden, assembly):
            raise RuntimeError('unexpected raster capability or resource profile')
        record = dict(schema_version=1, case_id=f'agfx.{name}.compile.opt{level}', status='pass',
            language='Rust', stage='vertex+fragment+compute' if args.views or args.multisample else 'vertex+fragment', entries=entries,
            target='spirv-unknown-vulkan1.3', profile='agfx-multisample' if args.multisample else 'agfx-format-views' if args.views else 'agfx-ordinary-raster', payload_type='SPIR-V',
            payload_sha256=sha256(module), identity=identity, rustc_command=command,
            capabilities=caps, extensions=re.findall(r'OpExtension "([^"]+)"', assembly))
        (output / f'{stem}.metadata.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({k: record[k] for k in ('case_id','status','payload_sha256','capabilities')}))


if __name__ == '__main__':
    main()
