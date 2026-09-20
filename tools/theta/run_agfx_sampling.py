"""Compare the source sampler goldens and independent seed/border oracles."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid

from run_agfx_copy import ROOT, require, sha256, validation_messages
from run_log import RunLog
import source_flip

SOURCES = ['Cargo.toml', 'Cargo.lock', 'crates/agfx/Cargo.toml', 'crates/agfx/src/lib.rs',
           'crates/agfx/src/vulkan.rs', 'crates/agfx/src/vulkan/compute.rs',
           'crates/agfx/src/vulkan/ownership.rs', 'crates/agfx/src/vulkan/sampler.rs',
           'crates/agfx/src/vulkan/texture.rs', 'crates/agfx/src/bin/texture_sampling.rs',
           'shaders/rust/texture_sampling.rs']
NAMES = ['seed', 'filter_nearest', 'filter_linear', 'address_repeat',
         'address_mirrored_repeat', 'address_clamp_to_edge', 'address_border', 'sample_2d']
CONTROLS = ['nonfinite_bias', 'reversed_lod', 'no_view_usage'] + [f'{name}.opt{level}'
    for level in (0, 3) for name in ('foreign_sampled', 'missing_sampler', 'foreign_sampler',
        'comparison_color', 'uninitialized_sampled', 'missing_sampled_usage', 'missing_storage_usage')]


def root(name):
    if name == 'seed':
        return struct.pack('<4I', 0, 0, 64, 64)
    scale, offset = (3.0, -1.0) if name.startswith('address_') else (.25, .375)
    return struct.pack('<6I6f', 0, 0, 0, 64, 64, 1, scale, scale, offset, offset, 0, 0)


def expected_outputs():
    from PIL import Image
    manifest = json.loads((ROOT / 'tests/parity/sampling-sources.json').read_text())
    goldens = {}
    for row in manifest['goldens']:
        path = ROOT / row['path']
        require(sha256(path.read_bytes()) == row['sha256'], 'changed original sampling golden')
        with Image.open(path) as image:
            require(image.size == (64, 64) and image.mode == 'RGBA', 'wrong source golden layout')
            goldens[path.stem] = image.tobytes()
    def texel(x, y):
        return bytes([(x * 255 + 31) // 63, (y * 255 + 31) // 63, 255 * ((x // 8 + y // 8) % 2), 255])
    expected = {'seed': b''.join(texel(x, y) for y in range(64) for x in range(64))}
    for name in NAMES[1:]:
        if name == 'address_border':
            # Nearest explicit LOD0: floor(64*(3*(pixel+.5)/64-1)).
            expected[name] = b''.join(texel(3*x-63, 3*y-63)
                if 0 <= 3*x-63 < 64 and 0 <= 3*y-63 < 64 else bytes(4)
                for y in range(64) for x in range(64))
        else:
            file_name = 'sample_2d' if name == 'sample_2d' else 'sampler_' + name.replace('address_', 'address_mode_')
            expected[name] = goldens[file_name]
    return expected


def structural_bounds(name, seed):
    """Independent integer texel/weight calculation, including UNORM floor/ceil.

    The sampled input is the separately checked seed capture. Sample2D instead
    uses its original, independently constructed integer-truncated upload.
    """
    lower, upper = bytearray(), bytearray()
    if name == 'seed':
        for y in range(64):
            for x in range(64):
                lower += bytes([x*255//63, y*255//63, 255*((x//8+y//8)%2), 255])
                upper += bytes([(x*255+62)//63, (y*255+62)//63, 255*((x//8+y//8)%2), 255])
        return lower, upper
    if name == 'sample_2d':
        seed = bytes(c for y in range(64) for x in range(64)
                     for c in [x*255//63,y*255//63,255*((x//8+y//8)%2),255])
    def pixel(x, y):
        if name == 'address_repeat': x, y = x%64, y%64
        elif name == 'address_mirrored_repeat':
            x, y = x%128, y%128
            x, y = min(x,127-x), min(y,127-y)
        elif name == 'address_border' and not (0 <= x < 64 and 0 <= y < 64): return bytes(4)
        else: x, y = min(63,max(0,x)), min(63,max(0,y))
        return seed[(y*64+x)*4:(y*64+x)*4+4]
    for y in range(64):
        for x in range(64):
            if name.startswith('address_'):
                # Both source linear and our added nearest case hit texel centers.
                value = pixel(3*x-63,3*y-63)
                lower += value
                upper += value
            elif name == 'filter_nearest':
                value = pixel((2*x+193)//8,(2*y+193)//8)
                lower += value
                upper += value
            else:
                ix, fx = divmod(2*x+189,8)
                iy, fy = divmod(2*y+189,8)
                taps = [pixel(ix,iy),pixel(ix+1,iy),pixel(ix,iy+1),pixel(ix+1,iy+1)]
                weights = [(8-fx)*(8-fy),fx*(8-fy),(8-fx)*fy,fx*fy]
                for channel in range(4):
                    numerator = sum(p[channel]*w for p,w in zip(taps,weights))
                    lower.append(numerator//64)
                    upper.append((numerator+63)//64)
    return lower, upper


def acceptance(comparisons, outputs, expected, flip_records):
    """Keep exact diagnostics separate from the source oracle and extra controls."""
    for row in comparisons:
        name, level = row['case_id'].removeprefix('agfx.sampling.').split('.opt')
        actual = outputs[row['case_id']]
        bounds = structural_bounds(name, outputs[f'agfx.sampling.seed.opt{level}'])
        bad = next((i for i,(value,lo,hi) in enumerate(zip(actual,*bounds)) if not lo <= value <= hi), None)
        row['structural_status'] = 'pass' if bad is None else 'fail'
        row['structural_first_difference'] = None if bad is None else dict(x=(bad//4)%64,y=(bad//4)//64,
            channel=bad%4,actual=actual[bad],lower=bounds[0][bad],upper=bounds[1][bad])
        alpha_ok = actual[3::4] == expected[name][3::4]
        row['alpha_status'] = 'pass' if alpha_ok else 'fail'
        flip = flip_records.get(row['case_id'])
        if name not in ('seed','address_border'):
            require(flip is not None and flip['reference_sha256'] == sha256(expected[name]) and
                    flip['actual_sha256'] == sha256(actual) and flip['threshold'] == 0.05 and
                    all(isinstance(flip[k], (float,int)) and 0 <= flip[k] <= 1 for k in ('mean','maximum')),
                    'missing or incorrect source FLIP oracle')
        row['source_flip'] = flip
        row['exact_status'] = row.pop('status')
        row['status'] = 'pass' if bad is None and alpha_ok and (flip is None or source_flip.passed(flip['mean'])) else 'fail'
    return comparisons


def check_record(record, token, identity, artifacts, outputs, expected):
    require(record.get('schema_version') == 1 and record.get('run_token') == token and record.get('status') == 'executed', 'version/token/status mismatch')
    require(record.get('host_source_sha256') == identity and record.get('shaders') == artifacts, 'stale host or shader identity')
    require(record.get('required') == record.get('executed') == 16, 'wrong case denominator')
    ids = [f'agfx.sampling.{name}.opt{level}' for level in (0, 3) for name in NAMES]
    cases = record.get('cases', [])
    require([row.get('case_id') for row in cases] == ids, 'missing, duplicate or reordered sampling case')
    controls = record.get('controls', [])
    require([row.get('case_id') for row in controls] == CONTROLS and all(row.get('status') == 'pass' for row in controls), 'missing or failed native control')
    require(record.get('comparison_creations') == 8, 'missing comparison sampler creation')
    device = record.get('device', {})
    require(device.get('validation') is True and device.get('synchronization_validation') is True, 'validation disabled')
    require(min(device.get('api_version', 0), device.get('loader_api_version', 0)) >= (1 << 22 | 4 << 12), 'wrong Vulkan API')
    comparisons = []
    for index, row in enumerate(cases):
        name = NAMES[index % 8]
        actual = outputs[row['case_id']]
        first_completion = (index // 8) * 20 + 4 + (index % 8) * 2 + (name == 'sample_2d')
        require(row.get('status') == 'executed' and row.get('bytes') == len(actual) == 16384, 'incomplete sampling output')
        require(row.get('sha256') == sha256(actual), 'wrong sampling output identity')
        require(row.get('root_bytes') == len(root(name)) and row.get('root_sha256') == sha256(root(name)), 'changed source root/inputs')
        require(row.get('completion_values') == [first_completion, first_completion + 1], 'wrong sampling completion sequence')
        golden = expected[name]
        diffs = [i for i, (a, b) in enumerate(zip(actual, golden)) if a != b]
        first = diffs[0] if diffs else None
        comparisons.append(dict(case_id=row['case_id'], status='fail' if diffs else 'pass',
            actual_sha256=sha256(actual), expected_sha256=sha256(golden),
            differing_pixels=len({i // 4 for i in diffs}), differing_channels=len(diffs),
            max_channel_error=max((abs(a-b) for a,b in zip(actual,golden)), default=0),
            first_difference=None if first is None else dict(x=(first//4)%64, y=(first//4)//64,
                channel=first%4, actual=actual[first], expected=golden[first])))
    return comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'sdk', 'shader-dir', 'output-dir', 'flip-reference'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'result.json').unlink(missing_ok=True)
    token, started = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id='agfx.sampling.run', run_token=token, status='fail', required=16, executed=0, passed=0,
                  runner_sha256=sha256(Path(__file__).read_bytes()),
                  oracle_source_sha256=sha256(Path(source_flip.__file__).read_bytes()))
    try:
        exe, sdk, shaders = args.executable.resolve(strict=True), args.sdk.resolve(strict=True), args.shader_dir.resolve(strict=True)
        identity = {path: sha256((ROOT / path).read_bytes()) for path in SOURCES}
        expected = expected_outputs()
        report['flip_identity'] = source_flip.identity(args.flip_reference)
        artifacts = [json.loads((shaders / f'texture_sampling_opt{level}.metadata.json').read_text()) for level in (0, 3)]
        for level, artifact in zip((0,3), artifacts):
            require(sha256((shaders / f'texture_sampling_opt{level}.spv').read_bytes()) == artifact['payload_sha256'], 'stale shader payload')
            for name in NAMES:
                (output / f'agfx.sampling.{name}.opt{level}.rgba8').unlink(missing_ok=True)
        built = subprocess.run([str(exe), '--identity'], capture_output=True, check=True, timeout=10)
        require(json.loads(built.stdout) == identity, 'stale executable rejected before Vulkan')
        report.update(host_source_sha256=identity, executable_sha256=sha256(exe.read_bytes()),
            validation_layer_sha256=sha256((sdk / 'Bin/VkLayer_khronos_validation.dll').read_bytes()))
        environment = os.environ.copy()
        for key in ('VK_LAYER_ENABLES', 'VK_LAYER_DISABLES', 'VK_LAYER_SETTINGS_PATH'):
            environment.pop(key, None)
        environment.update(VK_LOADER_LAYERS_DISABLE='~implicit~', VK_LAYER_PATH=str(sdk / 'Bin'),
            VK_LOADER_DEBUG='layer', VK_LAYER_VALIDATE_CORE='1', VK_LAYER_VALIDATE_SYNC='1')
        environment['PATH'] = str(sdk / 'Bin') + os.pathsep + environment['PATH']
        command = [str(exe), '--shader-dir', str(shaders), '--run-token', token]
        report.update(command=command, environment={k:v for k,v in environment.items() if k.startswith('VK_')}, executed=None)
        log.event('native_started', command=command)
        result = subprocess.run(command, cwd=output, env=environment, capture_output=True, timeout=60)
        (output / 'native.stdout.txt').write_bytes(result.stdout)
        (output / 'native.stderr.txt').write_bytes(result.stderr)
        stdout, stderr = result.stdout.decode('utf-8', 'replace'), result.stderr.decode('utf-8', 'replace')
        diagnostics, notices = validation_messages(stdout + stderr)
        inserted = 'Insert instance layer "VK_LAYER_KHRONOS_validation"' in stderr and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in stderr
        report.update(exit_code=result.returncode, validation_inserted=inserted, validation_diagnostics=diagnostics, intentional_loader_notices=notices)
        require(result.returncode == 0 and inserted and not diagnostics, 'native exit or validation failed')
        record = json.loads(stdout)
        outputs = {row['case_id']:(output / (row['case_id'] + '.rgba8')).read_bytes() for row in record['cases']}
        comparisons = check_record(record, token, identity, artifacts, outputs, expected)
        report.update(executed=16, execution_passed=16, native=record)
        flip_records = {row['case_id']: source_flip.evaluate(args.flip_reference, output, row['case_id'],
                        expected[NAMES[index%8]], outputs[row['case_id']])
                        for index,row in enumerate(comparisons) if NAMES[index%8] not in ('seed','address_border')}
        comparisons = acceptance(comparisons, outputs, expected, flip_records)
        passed = sum(row['status'] == 'pass' for row in comparisons)
        report.update(status='pass' if passed == 16 else 'fail', executed=16, passed=passed, native=record, comparisons=comparisons)
    except subprocess.TimeoutExpired as error:
        (output / 'native.stdout.txt').write_bytes(error.stdout or b'')
        (output / 'native.stderr.txt').write_bytes(error.stderr or b'')
        report.update(failure='native timeout', timed_out=True)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        report['failure'] = str(error)
    except KeyboardInterrupt:
        report.update(status='incomplete', failure='interrupted')
    report['elapsed_seconds'] = time.monotonic() - started
    log.finish(report)
    print(json.dumps({k:report.get(k) for k in ('case_id','status','failure','required','executed','passed','validation_inserted','elapsed_seconds')}))
    return report['status'] != 'pass'


if __name__ == '__main__':
    raise SystemExit(main())
