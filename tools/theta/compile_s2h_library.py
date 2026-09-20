"""Compile ShaderToHuman library/fixture modules. This does not dispatch a GPU."""
import argparse
import json
from pathlib import Path
import re

from shader_build import compiler_inputs, one, run, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rustgpu-source', type=Path, required=True)
    parser.add_argument('--codegen-backend', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--fixtures', action='store_true', help='compile all five shared source fixture entries')
    parser.add_argument('--images', action='store_true', help='compile fixtures with original RGBA8 storage-image output')
    args = parser.parse_args()
    if args.images:
        args.fixtures = True
    upstream = args.rustgpu_source.resolve(strict=True)
    backend = args.codegen_backend.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    source = root / 'crates/shader-to-human/src/lib.rs'
    entry = root / ('shaders/rust/shader_to_human_images.rs' if args.images else
                    'shaders/rust/shader_to_human_fixtures.rs' if args.fixtures else
                    'shaders/rust/shader_to_human_library.rs')
    entries = ['gather_cs', 'scatter_cs', 'table_cs', 'two_d_cs', 'world_cs'] if args.fixtures else ['main_cs']
    case = 'images' if args.images else 'fixtures' if args.fixtures else 'library'
    common, identity, validator, disassembler = compiler_inputs(
        upstream, backend, source, 'spirv-unknown-vulkan1.3')
    libm = one((upstream / 'target/compiletest-deps/spirv-unknown-vulkan1.3/debug/build/libm')
               .glob('*/out/liblibm*.rlib'), 'libm')
    common = [arg for arg in common if arg != '-Zcrate-attr=feature(asm_experimental_arch)']
    common += ['--extern', 'libm=' + str(libm)]
    for level in (0, 3):
        (output / f'{case}_opt{level}.metadata.json').unlink(missing_ok=True)
    for level in (0, 3):
        stem = f'{case}_opt{level}'
        library = output / f'libshader_to_human_opt{level}.rlib'
        library_command = [arg.replace('--crate-type=dylib', '--crate-type=rlib')
                           .replace('--crate-name=lib', '--crate-name=shader_to_human')
                           for arg in common]
        library_command += [f'-Copt-level={level}', '-o', str(library)]
        run(library_command, upstream, output / f'{stem}_rlib')
        module = output / f'{stem}.spv'
        command = [str(entry) if arg == str(source) else
                   arg.replace('--crate-name=lib', '--crate-name=s2h_library') for arg in common]
        command += ['--extern', 'shader_to_human=' + str(library),
                    f'-Copt-level={level}', '-o', str(module) + '.json']
        run(command, upstream, output / f'{stem}_rustc')
        manifest = json.loads(Path(str(module) + '.json').read_text(encoding='utf-8'))
        if sorted(manifest['entry_points']) != entries:
            raise RuntimeError('unexpected ShaderToHuman entry point')
        validation_command = [validator, '--target-env', 'vulkan1.3', str(module)]
        run(validation_command, upstream, output / f'{stem}_validate')
        disassembly_command = [disassembler, str(module)]
        assembly = run(disassembly_command, upstream, output / f'{stem}_disassemble')
        (output / f'{stem}.spvasm').write_text(assembly, encoding='utf-8')
        capabilities = re.findall(r'^\s*OpCapability (\w+)', assembly, re.MULTILINE)
        expected_caps = {'Shader', 'VulkanMemoryModel'}
        if set(capabilities) != expected_caps:
            raise RuntimeError(f'library capabilities changed: {capabilities}')
        record = dict(schema_version=1, case_id=f's2h.{case}.compile.opt{level}', status='pass',
                      scope='compilation and validation only, no GPU dispatch',
                      language='Rust', stage='compute', entry_points=entries,
                      target='spirv-unknown-vulkan1.3',
                      profile='ordinary-storage-image' if args.images else 'ordinary-storage-buffer',
                      payload_type='SPIR-V', payload_sha256=sha256(module), identity=identity,
                      library_sources={p.name: sha256(p) for p in source.parent.glob('*.rs')},
                      entry_sha256=sha256(entry), library_sha256=sha256(library),
                      fixture_sources={p.name: sha256(p) for p in
                                       (root / 'crates/shader-to-human/fixtures').glob('*.rs')} if args.fixtures else {},
                      libm_sha256=sha256(libm), capabilities=capabilities,
                      commands=[library_command, command, validation_command, disassembly_command])
        (output / f'{stem}.metadata.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({k: record[k] for k in ['case_id', 'status', 'payload_sha256', 'capabilities']}))


if __name__ == '__main__':
    main()
