"""Run the full Gaussian graph with PLY, depth, MSAA and temporal oracles."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid

import numpy as np
from run_agfx_copy import ROOT, require, sha256, validation_messages
from run_log import RunLog
from run_s2h_hello import srgb

SOURCES = ['Cargo.toml', 'Cargo.lock', 'crates/agfx/Cargo.toml', 'crates/agfx/src/lib.rs',
           'crates/agfx/src/vulkan.rs', 'crates/shader-to-human/Cargo.toml',
           'crates/shader-to-human/examples/gaussian.rs', 'shaders/rust/shader_to_human_gaussian.rs']
SOURCES += ['crates/agfx/src/vulkan/' + n + '.rs' for n in
            ('compute', 'bindings', 'graphics', 'ownership', 'sampler', 'texture')]
SOURCES += ['crates/shader-to-human/src/' + n + '.rs' for n in
            ('lib', 'font', 'math', 'gather', 'widgets', 'scatter', 'world')]
SOURCES += ['crates/shader-to-human/programs/gaussian/' + n + '.rs' for n in ('mod', 'math', 'ply', 'programs')]
SOURCES += ['tests/parity/fixtures/shader-to-human/gaussian-' + n for n in
            ('debug.ply.bin', 'cameras.txt', 'inputs.json')]
NAMES = ['saved', 'frozen-frame17', 'random-frame1', 'random-frame2',
         'near-camera', 'reverse-camera', 'offset', 'clipped']
TARGETS = ['C0', 'C1', 'C3', 'C4', 'C5']


def artifact(row, expected_name, size, read):
    name = row['output']
    require(name == expected_name and Path(name).name == name, 'wrong artifact name')
    data = read(name)
    require(len(data) == row['bytes'] == size and sha256(data) == row['sha256'], 'corrupt/incomplete artifact: ' + name)
    return data


def check_header(post, expected):
    require(len(post) == len(expected) == 51520 and post[:16] == struct.pack('<4I', 382, 62, 0, 200)
            and post[16:] == expected[16:], 'wrong header or input mutation')


def check_samples(image, samples, depth, label):
    require(image.shape == (600, 800, 4) and samples.shape == (8, 8, 8, 4) and depth.shape == (8, 8, 8),
            'incomplete MSAA shape: ' + label)
    require(np.isfinite(samples).all() and np.all((samples >= 0) & (samples <= 1)), 'invalid MSAA samples: ' + label)
    require(np.isfinite(depth).all() and np.all((depth >= 0) & (depth <= 1)), 'invalid MSAA depth: ' + label)
    require(np.array_equal(samples[..., 3] == 1, depth > 0), 'MSAA coverage/depth disagreement: ' + label)
    # Vulkan float-to-UNORM permits either neighboring integer code. Alpha0.5
    # maps to127.5, so127 and128 are both valid. No broader alpha tolerance.
    clear_alpha = samples[..., 3][depth == 0]
    require(np.all((np.abs(clear_alpha-127/255) < 1e-6) | (np.abs(clear_alpha-128/255) < 1e-6)),
            'MSAA clear alpha: ' + label)
    rgb = samples[..., :3].astype(np.float64)
    decoded = np.where(rgb <= .04045, rgb/12.92, ((rgb+.055)/1.055)**2.4)
    wanted = np.rint(srgb(np.sum(decoded, axis=2)/8.0001)*255)
    y, x = np.mgrid[:8, :8]
    observed = image[(y+1)*600//9, (x+1)*800//9, :3].astype(float)
    require(np.max(np.abs(observed-wanted)) <= 1, 'independent eight-sample resolve: ' + label)


def inputs():
    steps = json.loads((ROOT/'tests/parity/fixtures/shader-to-human/gaussian-inputs.json').read_text(encoding='utf-8'))
    require([s['name'] for s in steps] == NAMES, 'missing/reordered fixed inputs')
    cameras = [list(map(float, row.split())) for row in
               (ROOT/'tests/parity/fixtures/shader-to-human/gaussian-cameras.txt').read_text(encoding='utf-8').splitlines()]
    require(len(cameras) == 3 and all(len(c) == 68 and np.isfinite(c).all() for c in cameras), 'invalid cameras')
    ply = (ROOT/'tests/parity/fixtures/shader-to-human/gaussian-debug.ply.bin').read_bytes()
    require(len(ply) == 51128 and sha256(ply) == 'd4309642c238abed54ecfc7426404735457b08a0aec9b40302def929fa4ee28f', 'stale original PLY')
    values = []
    for s in steps:
        floats = cameras[s['camera']] + [800, 600, 0, 1000, 100, 100, 0, 0] + s['offset'] + [0] + s['ray_bounds'] + [0, 0]
        values.append(bytes(16) + struct.pack('<84f4I4f', *floats, s['frame'], s['random'], 0, 0, 0, 1, 1, 0) + ply + bytes(8))
    require(all(len(v) == 51520 for v in values), 'invalid flat input ABI')
    return steps, values


def check_record(record, token, identity, metadata, level, read):
    require(record.get('run_token') == token and record.get('status') == 'pass'
            and record.get('optimization_level') == level, 'missing/foreign native result')
    require(record.get('host_source_sha256') == identity and record.get('shader') == metadata,
            'stale native source/shader identity')
    require(record.get('required') == record.get('executed') == 40, 'missing/zero-case execution')
    cases = record.get('cases', [])
    expected_ids = [f's2h.gaussian.{name}.opt{level}.{target}' for name in NAMES for target in TARGETS]
    require([c['case_id'] for c in cases] == expected_ids, 'missing/reordered source targets')
    steps, expected_inputs = inputs()
    buffers = record.get('buffers', [])
    require(len(buffers) == 8, 'missing PLY/input readback')

    for row, step, expected in zip(buffers, steps, expected_inputs):
        require(row['case_id'] == f's2h.gaussian.{step["name"]}.opt{level}' and row['step'] == step, 'wrong PLY case')
        require(artifact(row['input'], row['case_id']+'.input', 51520, read) == expected, 'wrong immutable inputs')
        check_header(artifact(row['post'], row['case_id']+'.post', 51520, read), expected)
    views = {}
    sampled_pixels = 0
    for case in cases:
        cid = case['case_id']; target = cid.rsplit('.', 1)[-1]
        name = cid.removeprefix('s2h.gaussian.').rsplit('.opt', 1)[0]
        require(case.get('status') == 'pass', 'failed native target: ' + cid)
        done = case['completion_values']
        require(len(done) >= 2 and all(isinstance(v, int) and v > 0 for v in done)
                and all(b > a for a, b in zip(done, done[1:])), 'unordered completion: ' + cid)
        image = np.frombuffer(artifact(case['image'], cid+'.rgba8', 800*600*4, read), np.uint8).reshape(600, 800, 4)
        views[name, target] = image
        if target in ('C0', 'C1', 'C3'):
            base = np.frombuffer(artifact(case['base'], cid+'.base.rgba8', 800*600*4, read), np.uint8).reshape(600, 800, 4)
            require(np.all(image[..., 3] == 255) and np.all(base[..., 3] == 255), 'compute alpha: ' + cid)
            # The text/ramp base leaves this far lower-right point at source background.
            expected = np.rint(srgb(np.array([.07, .14, .21]))*255)
            require(np.max(np.abs(base[599, 799, :3].astype(float)-expected)) <= 1, 'source background: ' + cid)
        elif target == 'C4':
            depth = np.frombuffer(artifact(case['depth'], cid+'.depth', 800*600*4, read), '<f4').reshape(600, 800)
            require(np.isfinite(depth).all() and np.all((depth >= 0) & (depth <= 1)), 'invalid single-sample depth: ' + cid)
            alpha = image[..., 3]
            require(np.isin(alpha, [0, 255]).all() and np.array_equal(alpha == 255, depth > 0), 'coverage/depth disagreement: ' + cid)
            require(np.all(image[depth == 0] == 0), 'unwritten C4 pixels changed: ' + cid)
        else:
            require(np.all(image[..., 3] == 255), 'resolve alpha: ' + cid)
            samples = np.frombuffer(artifact(case['samples'], cid+'.samples', 8192, read), '<f4').reshape(8, 8, 8, 4)
            depth = np.frombuffer(artifact(case['depth_samples'], cid+'.depth-samples', 8192, read), '<f4').reshape(8, 8, 8, 4)[..., 0]
            check_samples(image, samples, depth, cid)
            sampled_pixels += 64
    for target in TARGETS:
        require(np.array_equal(views['saved', target], views['frozen-frame17', target]), 'disabled frameRandom changed ' + target)
    for target in ('C0', 'C1'):
        require(np.array_equal(views['saved', target], views['random-frame1', target]), 'nonrandom program changed ' + target)
    for target in ('C3', 'C4', 'C5'):
        require(not np.array_equal(views['random-frame1', target], views['random-frame2', target]), 'frameRandom inactive ' + target)
    require(np.array_equal(views['saved', 'C4'], views['offset', 'C4']), 'PLY unexpectedly uses SplatOffset')
    require(np.array_equal(views['saved', 'C0'], views['clipped', 'C0']), 'raster unexpectedly uses ray clipping')
    require(np.array_equal(views['saved', 'C4'], views['clipped', 'C4']), 'PLY raster unexpectedly uses ray clipping')
    require(np.count_nonzero(views['saved', 'C4'][..., 3]) > 100, 'no visible original PLY splats')
    require(not np.array_equal(views['saved', 'C4'], views['near-camera', 'C4']), 'camera does not change PLY projection')
    device = record.get('device', {})
    require(device.get('shader_int64') is True and device.get('signed_zero_inf_nan_preserve_f32') is True,
            'missing required shader features')
    require(device.get('validation') is True and device.get('synchronization_validation') is True
            and min(device.get('api_version', 0), device.get('loader_api_version', 0)) >= (1 << 22 | 4 << 12), 'missing Vulkan1.4/validation')
    return dict(cases=40, ply_buffers=16, depth_images=8, msaa_sample_values=8192, resolve_pixels=sampled_pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'sdk', 'shader-dir', 'output-dir', 'review'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id='s2h.gaussian.execution', status='fail', run_token=token,
                  required=80, executed=0, source_image_agreement='not checked', runs=[], runner_sha256=sha256(Path(__file__).read_bytes()))
    try:
        exe, sdk, shaders, review_path = (p.resolve(strict=True) for p in (args.executable, args.sdk, args.shader_dir, args.review))
        identity = {name: sha256((ROOT/name).read_bytes()) for name in SOURCES}
        review = json.loads(review_path.read_text(encoding='utf-8'))
        report['executable_sha256'] = sha256(exe.read_bytes())
        require(review.get('status') == 'reviewed-before-dispatch' and review.get('host_sources') == identity
                and review.get('executable_sha256') == report['executable_sha256'], 'missing or stale pre-execution review')
        report['review_sha256'] = sha256(review_path.read_bytes())
        compiled = subprocess.run([str(exe), '--identity'], capture_output=True, check=True, timeout=10)
        require(json.loads(compiled.stdout) == identity, 'stale executable rejected before Vulkan')
        environment = os.environ.copy()
        for name in ('VK_LAYER_ENABLES', 'VK_LAYER_DISABLES', 'VK_LAYER_SETTINGS_PATH'): environment.pop(name, None)
        environment.update(VK_LOADER_LAYERS_DISABLE='~implicit~', VK_LAYER_PATH=str(sdk/'Bin'), VK_LOADER_DEBUG='layer', VK_LAYER_VALIDATE_CORE='1', VK_LAYER_VALIDATE_SYNC='1')
        environment['PATH'] = str(sdk/'Bin') + os.pathsep + environment['PATH']
        report['environment'] = {k: v for k, v in environment.items() if k.startswith('VK_')}
        for level in (0, 3):
            metadata = json.loads((shaders/f'gaussian_opt{level}.metadata.json').read_text(encoding='utf-8'))
            require(metadata['payload_sha256'] == sha256((shaders/f'gaussian_opt{level}.spv').read_bytes())
                    == review.get('payloads', {}).get(str(level)), 'unreviewed or stale payload')
            folder = output/f'opt{level}'; folder.mkdir(exist_ok=True)
            command = [str(exe), '--run-token', token, '--shader-dir', str(shaders), '--level', str(level)]
            log.event('native_started', level=level, command=command); report['executed'] = None
            try: result = subprocess.run(command, cwd=folder, env=environment, capture_output=True, timeout=300)
            except subprocess.TimeoutExpired as error:
                (folder/'native.stdout.txt').write_bytes(error.stdout or b''); (folder/'native.stderr.txt').write_bytes(error.stderr or b''); raise
            (folder/'native.stdout.txt').write_bytes(result.stdout); (folder/'native.stderr.txt').write_bytes(result.stderr)
            stdout, stderr = result.stdout.decode('utf-8', 'replace'), result.stderr.decode('utf-8', 'replace')
            diagnostics, notices = validation_messages(stdout+stderr)
            inserted = 'Insert instance layer "VK_LAYER_KHRONOS_validation"' in stderr and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in stderr
            require(result.returncode == 0 and inserted and not diagnostics, f'opt{level}: native/validation failure {diagnostics[:2]}')
            native = json.loads(stdout)
            checked = check_record(native, token, identity, metadata, level, lambda name: (folder/name).read_bytes())
            report['runs'].append(dict(level=level, native=native, checks=checked, validation=dict(inserted=inserted, diagnostics=diagnostics, notices=notices)))
            report['executed'] = 40*len(report['runs'])
            print(json.dumps(dict(level=level, status='pass', executed=40)), flush=True)
        require(sha256(exe.read_bytes()) == report['executable_sha256'], 'executable changed during run')
        report['status'] = 'pass'
    except (OSError, ValueError, KeyError, TypeError, AssertionError, subprocess.SubprocessError) as error:
        report['error'] = str(error); log.event('failed', error=str(error))
    finally:
        report['elapsed_seconds'] = time.monotonic()-start; log.finish(report)
    print(json.dumps({k: report.get(k) for k in ('case_id', 'status', 'required', 'executed', 'error')}))
    return report['status'] != 'pass'


if __name__ == '__main__':
    raise SystemExit(main())
