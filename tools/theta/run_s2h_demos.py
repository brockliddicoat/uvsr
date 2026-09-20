"""Verify all documentation executions and ordered UI state. Image parity is separate."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid

from run_agfx_copy import FEATURES, ROOT, require, sha256, validation_messages
from run_log import RunLog

COUNTS = dict(gather=7, scatter=6, **{'2d': 11, '3d': 6}, ui=6, intro=1)
DOCS = [(category, branch) for category, count in COUNTS.items() for branch in range(count)]
SOURCES = ["Cargo.toml", "Cargo.lock", "crates/agfx/Cargo.toml", "crates/agfx/src/lib.rs",
           "crates/agfx/src/vulkan.rs", "crates/agfx/src/vulkan/compute.rs",
        "crates/agfx/src/vulkan/bindings.rs", "crates/agfx/src/vulkan/graphics.rs",
           "crates/agfx/src/vulkan/ownership.rs", "crates/agfx/src/vulkan/sampler.rs", "crates/agfx/src/vulkan/texture.rs",
           "crates/shader-to-human/Cargo.toml", "crates/shader-to-human/examples/docs.rs",
           "shaders/rust/shader_to_human_demos.rs", "tests/parity/fixtures/shader-to-human/camera.txt"]
SOURCES += ['crates/shader-to-human/src/' + name + '.rs'
            for name in ['lib', 'font', 'math', 'gather', 'widgets', 'scatter', 'world']]
SOURCES += ['crates/shader-to-human/demos/' + name + '.rs'
            for name in ['mod', 'gather', 'scatter', 'two_d', 'ui', 'world']]

# Independent exact state oracle, from the named source widget endpoints.
# Tuples are name, branch, x, y, pressed, radio, checkbox, alpha, blue, rgba alpha.
STEPS = [
    ('radio-red', 1, 113, 17, 1, 1, 0, 0, 0, 0),
    ('radio-green', 1, 129, 17, 1, 2, 0, 0, 0, 0),
    ('radio-blue', 1, 145, 17, 1, 3, 0, 0, 0, 0),
    ('clear', 0, 146, 18, 1, 0, 0, 0, 0, 0),
    ('checkbox-idle', 2, 113, 17, 0, 0, 0, 0, 0, 0),
    ('checkbox-press', 2, 113, 17, 1, 0, 1, 0, 0, 0),
    ('checkbox-hold', 2, 113, 17, 1, 0, 1, 0, 0, 0),
    ('checkbox-release', 2, 113, 17, 0, 0, 1, 0, 0, 0),
    ('checkbox-second-press', 2, 113, 17, 1, 0, 0, 0, 0, 0),
    ('float-high', 3, 169, 15, 1, 0, 0, 1, 0, 0),
    ('float-outside', 3, 300, 15, 1, 0, 0, 1, 0, 0),
    ('float-low', 3, 44, 15, 1, 0, 0, 0, 0, 0),
    ('rgb-blue', 4, 169, 47, 1, 0, 0, 0, 1, 0),
    ('rgba-alpha', 5, 169, 64, 1, 0, 0, 0, 1, 1),
    ('rgba-release', 5, 169, 64, 0, 0, 0, 0, 1, 1),
]


def state_words(step):
    words = [0] * 20
    words[0], words[1] = step[5:7]
    words[7], words[6], words[11] = [0x3f800000 * value for value in step[7:10]]
    return words


def root_bytes(category, branch, mouse=(0, 0, 0, 0), previous=(0, 0, 0, 0)):
    camera = [float(value) for value in (ROOT / 'tests/parity/fixtures/shader-to-human/camera.txt').read_text().split()]
    require(len(camera) == 20, 'incomplete frozen camera')
    return struct.pack('<4I28f', 800, 600, category, branch, *camera, *mouse, *previous)


def check_image(record, name, level, actual):
    require(record == dict(output=f'{name}-opt{level}.rgba8', bytes=1920000, sha256=sha256(actual)), 'image record mismatch')
    require(len(actual) == 1920000 and any(actual), 'missing or incomplete image')
    require(actual[3::4] == bytes([255]) * 480000, 'source opaque alpha differs')


def check_record(record, token, identity, artifact, level, read):
    require(record.get('schema_version') == 1 and record.get('status') == 'pass'
            and record.get('run_token') == token and record.get('optimization_level') == level, 'stale or failed run')
    require(record.get('host_source_sha256') == identity and record.get('shader') == artifact, 'stale source or shader')
    require(record.get('required') == record.get('executed') == len(DOCS), 'incomplete required documentation run')
    rows = record.get('cases', [])
    require(len(rows) == len(DOCS), 'missing or duplicate documentation cases')
    last = 2  # Constructor cleared the image and initialized the state buffer.
    images = {}
    for (name, branch), row in zip(DOCS, rows):
        category_index = list(COUNTS).index(name)
        stem = f'docs-{name}-{branch}'
        require(row.get('case_id') == f's2h.{stem}.opt{level}' and row.get('status') == 'pass'
                and row.get('category') == category_index and row.get('branch') == branch, 'wrong documentation case')
        require(row.get('root_sha256') == sha256(root_bytes(category_index, branch)), 'wrong documentation inputs')
        # Each case first resets state with one copy, then renders and copies.
        require(row.get('completion_values') == [last + 2, last + 3], 'missing ordered documentation completion')
        last += 3
        require(row.get('source_agreement') == 'not checked', 'execution cannot imply source image parity')
        actual = read(f'{stem}-opt{level}.rgba8')
        check_image(row.get('image'), stem, level, actual)
        images[(name, branch)] = actual
    # Independent structural assertions from the source's empty branches and
    # an opaque rectangle interior, beyond the common image/alpha check.
    for branch in (9, 10):
        actual = images['2d', branch]
        require(actual == actual[:4] * 480000, 'source TODO branch unexpectedly drew geometry')
    require(images['2d', 9] == images['2d', 10], 'source empty branches differ')
    offset = (20 * 800 + 110) * 4
    require(images['2d', 4][offset:offset + 4] == bytes([255, 0, 0, 255]), 'opaque source rectangle interior differs')
    rows = record.get('interactions', [])
    require(len(rows) == len(STEPS), 'incomplete UI sequence')
    before, previous = bytes(80), [0, 0, 0, 0]
    last += 1  # Reset once before the persistent sequence.
    for step, row in zip(STEPS, rows):
        name, branch, x, y, pressed = step[:5]
        mouse = [x, y, pressed, 0]
        expected_words = state_words(step)
        expected = struct.pack('<20I', *expected_words)
        require(row.get('case_id') == f's2h.docs-ui.{name}.opt{level}' and row.get('status') == 'pass', 'wrong UI frame')
        require(row.get('branch') == branch and row.get('mouse') == mouse and row.get('previous_mouse') == previous, 'wrong input sequence')
        require(row.get('root_sha256') == sha256(root_bytes(4, branch, mouse, previous)), 'wrong UI root bytes')
        require(row.get('before_state_sha256') == sha256(before) and row.get('after_state_words') == expected_words, 'wrong persistent state transition')
        actual = read(f'ui-{name}-opt{level}.state')
        require(actual == expected, 'GPU state differs from independent source endpoint oracle')
        require(row.get('state') == dict(output=f'ui-{name}-opt{level}.state', bytes=80,
                sha256=sha256(expected), expected_sha256=sha256(expected)), 'state evidence mismatch')
        check_image(row.get('image'), f'ui-{name}', level, read(f'ui-{name}-opt{level}.rgba8'))
        require(row.get('completion_values') == list(range(last + 1, last + 5)), 'missing ordered render/update/copy completion')
        last += 4
        before, previous = expected, mouse
    device = record.get('device', {})
    require(device.get('validation') is True and device.get('synchronization_validation') is True, 'validation disabled')
    require(min(device.get('api_version', 0), device.get('loader_api_version', 0)) >= (1 << 22 | 4 << 12)
            and set(device.get('enabled_features', [])) == FEATURES, 'required native features missing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['executable', 'sdk', 'shader-dir', 'output-dir']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    (output / 'result.json').unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id='s2h.docs.execution', status='fail', run_token=token,
                  required=dict(docs=74, ui_frames=30), executed=dict(docs=0, ui_frames=0),
                  source_image_agreement='not checked', runs=[], runner_sha256=sha256(Path(__file__).read_bytes()))
    try:
        executable, sdk, shaders = (path.resolve(strict=True) for path in [args.executable, args.sdk, args.shader_dir])
        report['executable_sha256'] = sha256(executable.read_bytes())
        identity = {name: sha256((ROOT / name).read_bytes()) for name in SOURCES}
        compiled = subprocess.run([str(executable), '--identity'], capture_output=True, check=True, timeout=10)
        require(json.loads(compiled.stdout) == identity, 'stale executable rejected before Vulkan')
        environment = os.environ.copy()
        for name in ('VK_LAYER_ENABLES', 'VK_LAYER_DISABLES', 'VK_LAYER_SETTINGS_PATH'):
            environment.pop(name, None)
        environment.update(VK_LOADER_LAYERS_DISABLE='~implicit~', VK_LAYER_PATH=str(sdk / 'Bin'),
                           VK_LOADER_DEBUG='layer', VK_LAYER_VALIDATE_CORE='1', VK_LAYER_VALIDATE_SYNC='1')
        environment['PATH'] = str(sdk / 'Bin') + os.pathsep + environment['PATH']
        report['environment'] = {key: value for key, value in environment.items() if key.startswith('VK_')}
        for level in (0, 3):
            artifact = json.loads((shaders / f'demos_opt{level}.metadata.json').read_text())
            require(artifact['payload_sha256'] == sha256((shaders / f'demos_opt{level}.spv').read_bytes()), 'stale shader payload')
            folder = output / f'opt{level}'; folder.mkdir(exist_ok=True)
            # Remove only this runner's named outputs so stale captures cannot pass.
            for name, branch in DOCS:
                (folder / f'docs-{name}-{branch}-opt{level}.rgba8').unlink(missing_ok=True)
            for step in STEPS:
                for extension in ('rgba8', 'state'):
                    (folder / f'ui-{step[0]}-opt{level}.{extension}').unlink(missing_ok=True)
            command = [str(executable), '--run-token', token, '--shader-dir', str(shaders), '--level', str(level)]
            log.event('native_started', level=level, command=command)
            report['executed'] = None
            try:
                result = subprocess.run(command, cwd=folder, env=environment, capture_output=True, timeout=180)
            except subprocess.TimeoutExpired as error:
                (folder / 'native.stdout.txt').write_bytes(error.stdout or b'')
                (folder / 'native.stderr.txt').write_bytes(error.stderr or b'')
                raise
            (folder / 'native.stdout.txt').write_bytes(result.stdout); (folder / 'native.stderr.txt').write_bytes(result.stderr)
            text, err = result.stdout.decode('utf-8', 'replace'), result.stderr.decode('utf-8', 'replace')
            diagnostics, notices = validation_messages(text + err)
            inserted = ('Insert instance layer "VK_LAYER_KHRONOS_validation"' in err
                        and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in err)
            require(result.returncode == 0 and inserted and not diagnostics, f'opt{level}: native or validation failed: {diagnostics[:3]}')
            record = json.loads(text)
            check_record(record, token, identity, artifact, level, lambda name: (folder / name).read_bytes())
            require(sha256(executable.read_bytes()) == report['executable_sha256'], 'executable changed during run')
            report['runs'].append(dict(level=level, native=record, validation=dict(diagnostics=diagnostics, notices=notices)))
            report['executed'] = dict(docs=37 * len(report['runs']), ui_frames=15 * len(report['runs']))
            log.event('execution_verified', level=level, docs=37, ui_frames=15)
            print(json.dumps(dict(level=level, status='pass', docs=37, ui_frames=15)), flush=True)
        report['status'] = 'pass'
    except (OSError, ValueError, KeyError, TypeError, AssertionError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        log.event('failed', error=str(error))
    finally:
        report['elapsed_seconds'] = time.monotonic() - start
        log.finish(report)
    print(json.dumps({key: report[key] for key in ['case_id', 'status', 'required', 'executed']}))
    if report['status'] != 'pass':
        print(report.get('error')); raise SystemExit(1)


if __name__ == '__main__':
    main()
