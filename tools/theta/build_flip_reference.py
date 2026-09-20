"""Build the CPU image oracle against AGFX's exact existing FLIP header."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from shader_build import sha256

HEADER_SHA256 = 'e981618b68f8808f701cb2e8c88bfaadc6868ec4d81db2684cc9a0f874d06961'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--agfx-source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    header = args.agfx_source.resolve(strict=True) / 'src/agfx/agfx_tests/FLIP/FLIP.h'
    if sha256(header) != HEADER_SHA256:
        raise ValueError('FLIP header does not match the source pin')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name('flip_reference.cpp').resolve()
    exe = output / ('flip_reference.exe' if os.name == 'nt' else 'flip_reference')
    metadata = output / 'flip_reference.identity.json'
    metadata.unlink(missing_ok=True)
    if os.name == 'nt':
        # Run from the Visual Studio developer environment. No shell command is
        # composed from a path, and compilation writes only into the output dir.
        compiler = shutil.which('cl')
        if not compiler:
            raise ValueError('run this build from the Visual Studio developer environment')
        command = [compiler, '/nologo', '/EHsc', '/W4', '/WX', '/std:c++17', '/O2',
                   '/external:I' + str(header.parent), '/external:W0', str(source), '/Fe:' + str(exe)]
    else:
        compiler = shutil.which('c++')
        if not compiler:
            raise ValueError('a C++17 compiler is required for the source oracle')
        command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-isystem', str(header.parent), str(source), '-o', str(exe)]
    result = subprocess.run(command, cwd=output, capture_output=True, timeout=120)
    (output / 'build.stdout.txt').write_bytes(result.stdout)
    (output / 'build.stderr.txt').write_bytes(result.stderr)
    result.check_returncode()
    record = dict(schema_version=1, header_sha256=sha256(header), source_sha256=sha256(source),
                  executable_sha256=sha256(exe), compiler_sha256=sha256(Path(compiler)),
                  command=command, source_commit='f91b108a111d2ca3ca4b6586b6cb5dd750064fd7',
                  profile='AGFX default LDR-FLIP with RGBA8 divided by255, no extra sRGB conversion')
    metadata.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(record))


if __name__ == '__main__':
    main()
