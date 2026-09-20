"""Run full Features with source-state, image, font and ordering controls."""
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
from run_s2h_hello import srgb, close_codes

SOURCES = ['Cargo.toml', 'Cargo.lock', 'crates/agfx/Cargo.toml', 'crates/agfx/src/lib.rs',
           'crates/agfx/src/vulkan.rs', 'crates/shader-to-human/Cargo.toml',
           'crates/shader-to-human/examples/features.rs', 'crates/shader-to-human/fixtures/scatter.rs',
           'shaders/rust/shader_to_human_features.rs',
           'tests/parity/fixtures/shader-to-human/features-inputs.json',
           'tests/parity/fixtures/shader-to-human/features-cameras.txt']
SOURCES += ['crates/agfx/src/vulkan/' + name + '.rs' for name in
            ('compute', 'bindings', 'graphics', 'ownership', 'sampler', 'texture')]
SOURCES += ['crates/shader-to-human/src/' + name + '.rs' for name in
            ('lib', 'font', 'math', 'gather', 'widgets', 'scatter', 'world')]
SOURCES += ['crates/shader-to-human/programs/features/' + name + '.rs' for name in
            ('mod', 'gather', 'images', 'quad', 'table', 'two_d', 'world')]
PROGRAMS = ('world', 'gather', 'scatter', 'table', 'two_d', 'arrows', 'quad', 'font', 'coordinates')
NAMES = [f'{p}-camera{c}' for c in range(3) for p in PROGRAMS]
NAMES += ['gather-' + n for n in ('radio-green radio-hover radio-blue clear clear-release check check-hold '
          'check-release check-toggle alpha-start alpha-drag alpha-outside alpha-sentinel alpha-release '
          'red-start red-left red-release').split()]
NAMES += ['two_d-' + n for n in ('top-red top-red-release top-alpha top-alpha-release '
          'bottom-green bottom-release top-border border-release').split()]
NAMES += ['arrows-center', 'gather-mouse-outside']


def inputs():
    rows = json.loads((ROOT/'tests/parity/fixtures/shader-to-human/features-inputs.json').read_text(encoding='utf-8'))
    require([r['name'] for r in rows] == NAMES and len(rows) == 54, 'missing or reordered fixed inputs')
    return rows


def cameras():
    rows = [list(map(float, line.split())) for line in
            (ROOT/'tests/parity/fixtures/shader-to-human/features-cameras.txt').read_text(encoding='utf-8').splitlines()]
    require(len(rows) == 3 and all(len(r) == 56 and np.isfinite(r).all() for r in rows), 'invalid camera fixture')
    return rows


def seed(enabled):
    words = [0]*28
    if enabled:
        words[:4] = [2, 0, 0x12345678, 0x87654321]
        words[4:16] = struct.unpack('<12I', struct.pack('<12f', .2, .4, .8, .75, .8, .3, .1, .6, 3, 4, 5, 6))
        words[20:] = struct.unpack('<8I', struct.pack('<8f', 1, 2, 3, 4, 5, 6, 7, 8))
    return struct.pack('<28I', *words)


def input_bytes(state, step, previous, camera):
    return state + struct.pack('<68f', *camera[:52], *step['mouse'], *previous,
                               800, 600, step['time'], 1000, *camera[52:])


def expected_post(before, step):
    """Independent source cursor endpoints. No candidate rendering is called."""
    data = bytearray(before)
    name = step['name']
    floating = []
    def uint(word, value): struct.pack_into('<I', data, word*4, value)
    def value(word, number):
        struct.pack_into('<f', data, word*4, number)
        floating.append(word)
    if name == 'gather-radio-green': uint(0, 2)
    elif name == 'gather-radio-blue': uint(0, 3)
    elif name == 'gather-clear': uint(0, 0)
    elif name == 'gather-check': uint(1, 1)
    elif name == 'gather-check-toggle': uint(1, 0)
    if name in ('gather-alpha-start', 'gather-alpha-drag', 'gather-alpha-outside'):
        # Gather alpha: x42.5..170.5 outer, x44.5..168.5 inner, width124.
        value(7, min(1, max(0, (float(np.float32(step['mouse'][0]))-44.5)/124)))
        uint(16, 52); uint(17, 350)
    elif name in ('gather-red-start', 'gather-red-left'):
        # RGB leaves three characters for the color disc, then width5 at x90.
        value(4, min(1, max(0, (int(step['mouse'][0])-92.5)/76)))
        uint(16, 114); uint(17, 382)
    elif name in ('two_d-top-red', 'two_d-top-alpha', 'two_d-bottom-green'):
        word = {'two_d-top-red': 4, 'two_d-top-alpha': 7, 'two_d-bottom-green': 9}[name]
        value(word, min(1, max(0, (int(step['mouse'][0])-250.5)/76)))
        uint(16, int(step['mouse'][0])); uint(17, int(step['mouse'][1]))
    elif name == 'two_d-top-border':
        value(12, min(1, max(0, (int(step['mouse'][0])-202.5)/124))*20)
        uint(16, 300); uint(17, 170)
    # Gather/2D preserve float mouse, Table/Arrow explicitly convert to int4.
    mouse = step['mouse'] if step['program'] in ('gather', 'two_d') else [int(v) for v in step['mouse']]
    if step['program'] in ('gather', 'table', 'two_d', 'arrows') and mouse[2] == 0 and mouse[0] != -100:
        data[64:80] = bytes(16)
    return bytes(data), floating


def check_state(actual, expected, floating, label):
    require(len(actual) == len(expected) == 384, label + ': incomplete state')
    masked = bytearray(actual)
    for word in floating:
        want = struct.unpack_from('<f', expected, word*4)[0]
        got = struct.unpack_from('<f', actual, word*4)[0]
        require(np.isfinite(got) and abs(got-want) <= max(1e-7, abs(float(np.spacing(np.float32(want))))*2),
                f'{label}: state word{word}, expected{want}, observed{got}')
        masked[word*4:word*4+4] = expected[word*4:word*4+4]
    require(bytes(masked) == expected, label + ': unexpected state/input/padding change')


def sky_oracle(image, camera):
    y, x = np.mgrid[:600, :800]
    clip = np.stack([x/400-1, 1-y/300, np.full_like(x, camera[51], dtype=float), np.ones_like(x)], axis=-1)
    matrix = np.asarray(camera[:16]).reshape((4, 4), order='F')
    world = clip @ matrix.T
    ray = world[..., :3]/world[..., 3:4]-camera[48:51]
    ray /= np.linalg.norm(ray, axis=-1, keepdims=True)
    px = (-np.arctan2(ray[..., 2], ray[..., 0])/np.pi+1)*64
    py = np.arccos(ray[..., 1])/np.pi*32
    local_x = ((px/32+.5) % 1)*32
    grid = np.minimum.reduce([local_x % 1, 1-local_x % 1, py % 1, 1-py % 1])
    weight = .07*np.clip(1-grid*30, 0, 1)
    gray = np.clip(1-np.abs(ray[..., 1])**.2, 0, 1)
    result = gray*(1-weight)+weight
    # The only colored text occupies rows12..20 in source skybox font space.
    # " +/-X/Z" has an empty first character and no glyph after x24.
    # A horizontal camera can put every pixel inside the text's row band.
    mask = (py < 11.99) | (py > 20.01) | (local_x < 7.99) | (local_x > 24.01)
    # abs(y)^0.2 has an unbounded derivative at the horizon. This double CPU
    # oracle checks well-conditioned rays. Full source comparisons retain every
    # horizon pixel and report any difference instead of treating it as a pass.
    mask &= np.abs(ray[..., 1]) > 1e-5
    require(np.count_nonzero(mask) > 50000, 'insufficient sky oracle pixels')
    close_codes(image[mask, :3], np.repeat(result[mask, None], 3, axis=1), 'Clear source sky RGB')
    return int(np.count_nonzero(mask))


def font_oracle(image, atlas, time_value):
    require(atlas.shape == (8, 768, 4), 'wrong atlas extent')
    require(set(np.unique(atlas[..., 3])) == {0, 255}, 'font mask alpha is not binary')
    require(np.all(atlas[:, :8] == 0), 'space glyph not empty')
    y, x = np.mgrid[:8, :768]
    hue = np.float32(time_value)+(x+y)/16*.1
    v = (hue[..., None]*6+np.array([0, 4, 2])) % 6
    rgb = np.clip(np.abs(v-3)-1, 0, 1)
    rgb = rgb*rgb*(3-2*rgb)
    mask = atlas[..., 3] == 255
    require(np.count_nonzero(mask) > 500, 'empty font atlas')
    close_codes(atlas[mask, :3], rgb[mask], 'source HSV font color')
    expected = np.zeros((600, 800, 3))
    for index, char in enumerate('UserFont'):
        # Source glyph expansion is32x32 at cursor(10,10), scale4.
        source = atlas[:, (ord(char)-32)*8:(ord(char)-31)*8].astype(float)/255
        linear = np.where(source[..., :3] <= .04045, source[..., :3]/12.92,
                          ((source[..., :3]+.055)/1.055)**2.4)*source[..., 3:4]
        expected[10:42, 10+index*32:42+index*32] = np.repeat(np.repeat(linear, 4, axis=0), 4, axis=1)
    close_codes(image[..., :3], expected, 'sRGB atlas load, integer coordinates and glyph expansion')


def arrows_oracle(image):
    # Source first-column centers are clear of crosshairs and dynamic arrows.
    # Width zero covers half a texel. The seven wider lines cover it fully.
    for row in range(8):
        linear = .5 * (1 - min((row + 1) * .5, 1))
        close_codes(image[50 + row * 20, 40, :3], srgb(np.full(3, linear)), 'missing arrow row')
    close_codes(image[45, 20, :3], srgb(np.array([.75, .25, .25])), 'missing arrow crosshair')
    close_codes(image[599, 799, :3], srgb(np.full(3, .5)), 'arrow background lost to NaN')


def artifact(row, name, size, read):
    require(row.get('output') == name and row.get('bytes') == size, 'wrong artifact name/size')
    raw = read(name)
    require(len(raw) == size and row.get('sha256') == sha256(raw), 'stale or truncated artifact')
    return raw


def check_record(record, token, identity, metadata, level, read):
    require(record.get('schema_version') == 1 and record.get('status') == 'pass' and record.get('run_token') == token, 'stale/failed record')
    require(record.get('required') == record.get('executed') == 54 and len(record.get('cases', [])) == 54, 'incomplete case execution')
    require(record.get('host_source_sha256') == identity and record.get('shader') == metadata and record.get('optimization_level') == level, 'wrong source/payload identity')
    state, previous, completion = seed(False), [0, 0, 0, 0], 2
    views, checked = {}, 0
    camera_values = cameras()
    for step, row in zip(inputs(), record['cases']):
        name = step['name']; case = f's2h.features.{name}.opt{level}'
        require(row.get('case_id') == case and row.get('status') == 'pass' and row.get('step') == step, 'wrong case/step')
        if 'reset' in step: state, previous = seed(step['reset'] == 'seed'), [0, 0, 0, 0]
        require(row.get('previous_mouse') == np.asarray(previous, dtype=np.float32).astype(float).tolist(), 'wrong mouse history')
        before = input_bytes(state, step, previous, camera_values[step['camera']])
        require(artifact(row['input'], case+'.input', 384, read) == before, 'broken state/input chain')
        post = artifact(row['post'], case+'.post', 384, read)
        expected, floating = expected_post(before, step)
        check_state(post, expected, floating, name)
        state, previous = post[:112], step['mouse']
        image = np.frombuffer(artifact(row['image'], case+'.rgba8', 1920000, read), np.uint8).reshape(600, 800, 4)
        require(np.all(image[..., 3] == 255), name + ': output alpha must be opaque')
        program = step['program']
        count = 5
        if program in ('gather', 'quad', 'scatter', 'font'):
            is_font = program == 'font'
            shape = (8, 768, 4) if is_font else (600, 800, 4)
            ext = '.atlas.rgba8' if is_font else '.before.rgba8'
            raw = artifact(row['intermediate'], case+ext, int(np.prod(shape)), read)
            intermediate = np.frombuffer(raw, np.uint8).reshape(shape)
            count += 2 + (program == 'quad')
            if program == 'font': font_oracle(image, intermediate, step['time'])
            elif program == 'scatter':
                checked += sky_oracle(intermediate, camera_values[step['camera']])
                require(np.array_equal(image[:, :522], intermediate[:, :522]), 'Scatter changed untouched clear region')
                require(np.count_nonzero(np.any(image != intermediate, axis=-1)) > 3000, 'missing Scatter writes')
            elif program == 'gather':
                mx, my = map(int, step['mouse'][:2])
                if 0 <= mx < 800 and 0 <= my < 600:
                    require(np.array_equal(image[my, mx], intermediate[my, mx]), 'DebugZoom wrote its selected texel')
                else:
                    require(intermediate.shape == image.shape, 'missing offscreen mouse control')
                close_codes(intermediate[599, 799, :3], np.array([.4, .7, .4]), 'Gather raw UNORM background')
            elif program == 'quad':
                require(np.count_nonzero(np.any(image != intermediate, axis=-1)) > 5000, 'missing quad post overlay')
        else: require(row.get('intermediate') is None, 'unexpected intermediate')
        require(row.get('completion_values') == list(range(completion+1, completion+count+1)), 'wrong GPU pass/copy ordering')
        completion += count
        if program == 'coordinates': close_codes(image[599, 799, :3], srgb(np.array([.01, .01, .1])), 'coordinate sRGB background')
        if program == 'table': close_codes(image[599, 799, :3], np.array([.4, .7, .4]), 'Table raw UNORM background')
        if program == 'arrows': arrows_oracle(image)
        views[name] = image
    require(np.count_nonzero(np.any(views['quad-camera1'] != views['quad-camera2'], axis=-1)) > 10000, 'camera failed to change quad image')
    require(np.count_nonzero(np.any(views['world-camera1'] != views['world-camera2'], axis=-1)) > 10000, 'camera failed to change world image')
    device = record.get('device', {})
    if metadata.get('language') == 'Rust':
        require(device.get('signed_zero_inf_nan_preserve_f32') is True, 'missing float32 NaN preservation support')
    require(device.get('validation') is True and device.get('synchronization_validation') is True
            and min(device.get('api_version', 0), device.get('loader_api_version', 0)) >= (1 << 22 | 4 << 12), 'missing Vulkan1.4/validation')
    return dict(cases=54, sky_pixels=checked, state_buffers=108, font_images=3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'sdk', 'shader-dir', 'output-dir', 'review'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id='s2h.features.execution', status='fail', run_token=token,
                  required=108, executed=0, source_image_agreement='not checked', runs=[], runner_sha256=sha256(Path(__file__).read_bytes()))
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
            metadata = json.loads((shaders/f'features_opt{level}.metadata.json').read_text(encoding='utf-8'))
            require(metadata['payload_sha256'] == sha256((shaders/f'features_opt{level}.spv').read_bytes())
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
            report['executed'] = 54*len(report['runs'])
            print(json.dumps(dict(level=level, status='pass', executed=54)), flush=True)
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
