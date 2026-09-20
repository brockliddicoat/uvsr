"""Run Zoom2D with independent state, ordering, color and temporal oracles."""
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
           'crates/shader-to-human/examples/zoom.rs', 'crates/shader-to-human/programs/zoom.rs',
           'shaders/rust/shader_to_human_zoom.rs', 'tests/parity/fixtures/shader-to-human/zoom-inputs.json']
SOURCES += ['crates/agfx/src/vulkan/' + name + '.rs' for name in
            ('compute', 'bindings', 'graphics', 'ownership', 'sampler', 'texture')]
SOURCES += ['crates/shader-to-human/src/' + name + '.rs' for name in
            ('lib', 'font', 'math', 'gather', 'widgets', 'scatter', 'world')]
NAMES = ('idle pan-start pan-move pan-hold pan-release zoom-start zoom-half zoom-quarter '
         'zoom-sixteenth zoom-back zoom-release outside-start outside-drag outside-release '
         'reset-press reset-hold reset-release fraction-start fraction-drag fraction-release '
         'left-nonbinary-start left-nonbinary-drag right-nonbinary-start right-nonbinary-drag '
         'both-start both-move both-release final-reset final-idle').split()


def inputs():
    rows = json.loads((ROOT/'tests/parity/fixtures/shader-to-human/zoom-inputs.json').read_text(encoding='utf-8'))
    require([r[0] for r in rows] == NAMES and all(len(r) == 5 for r in rows), 'invalid input sequence')
    require(all(np.isfinite(r[1:]).all() for r in rows), 'nonfinite input')
    return rows


def expected_states(index):
    # Independent source endpoints. Each50 vertical drag pixels halves scale.
    if index < 2 or 15 <= index <= 17 or index == 28: pan = (0, 0, 0)
    elif index <= 5: pan = (-24, -16, 0)
    elif index == 6: pan = (152, 118, -50)
    elif index in (7, 9, 10, 11): pan = (504, 386, -100)
    elif index == 8: pan = (2616, 1994, -200)
    elif index <= 14: pan = (-1996, -1414, -100)
    elif index <= 22: pan = (-1, 0, 0)
    elif index <= 24: pan = (198, 150, -50)
    else: pan = (576, 350, -100)
    if index == 0: drag = (0, 0, 0, 0)
    elif index <= 4: drag = (100, 100, 0, 0)
    elif index <= 10: drag = (100, 100, 200, 150)
    elif index <= 13: drag = (0, 0, 200, 150)
    elif index <= 16 or index >= 27: drag = (40, 538, 200, 150)
    elif index <= 23: drag = (100, 100, 200, 150)
    else: drag = (200, 250, 200, 150)
    pre = bytes(80) + struct.pack('<8f', *pan, 0, *drag)
    post = bytes(80) + struct.pack('<8f', *(pan if index not in (14, 15, 27) else (0, 0, 0)), 0, *drag)
    return pre, post


def root_bytes(mouse, previous):
    return struct.pack('<4I8f', 800, 600, 0, 0, *mouse, *previous)


def structural(images):
    for name, image in images.items():
        require(image.shape == (600, 800, 4) and np.all(image[..., 3] == 255), name + ': nonopaque or incomplete image')
    for name in ('outside-drag', 'outside-release', 'reset-press'):
        close_codes(images[name][:470, :, :3], srgb(np.array([.01, .01, .1])), name + ' outside background')
    require(np.array_equal(images['idle'], images['final-idle']), 'reset did not restore initial frame')
    require(np.array_equal(images['pan-move'], images['pan-hold']), 'stationary drag changed frame')
    require(np.array_equal(images['pan-hold'], images['pan-release']), 'release changed frame')
    require(np.array_equal(images['idle'][:470], images['reset-hold'][:470]), 'post reset not visible next frame')
    require(np.count_nonzero(np.any(images['idle'] != images['pan-move'], axis=-1)) > 5000, 'pan did not move geometry')
    require(np.count_nonzero(np.any(images['zoom-quarter'] != images['zoom-sixteenth'], axis=-1)) > 5000, 'magnification did not change image')
    # Grid/background oracle away from source geometry, labels, and screen UI.
    y, x = np.mgrid[:600, :800]
    checked = 0
    for index, name in enumerate(NAMES):
        pan = np.array(struct.unpack('<8f', expected_states(index)[0][80:])[:3])
        scale = 2 ** (pan[2] * .02)
        px, py = (x + .5 + pan[0]) * scale, (y + .5 + pan[1]) * scale
        mask = (y < 470) & ((px > 510) | (py > 400) | (px < -2) | (py < -2))
        if scale < .125: mask &= (py - np.floor(py)) * 40 + .5 > 22
        # Negative(-1,0) uses source integer truncation, not floor.
        if name == 'fraction-drag': mask[0, 0] = True
        width = scale + .01
        def integrate(p):
            a, b = p + .025 + .5*width, p + .025 - .5*width
            return (np.floor(a) + np.minimum((a-np.floor(a))*20,1)
                    - np.floor(b) - np.minimum((b-np.floor(b))*20,1)) / (20*width)
        alpha = 1 - (1-integrate(px))*(1-integrate(py))
        outside = (np.trunc(px) < 0) | (np.trunc(py) < 0) | (np.trunc(px) >= 800) | (np.trunc(py) >= 600)
        alpha[outside] = 0
        weight = alpha[mask, None] * .05
        linear = np.array([.01,.01,.1]) * (1-weight) + weight
        close_codes(images[name][mask, :3], srgb(linear), name + ' grid and gamma')
        checked += int(np.count_nonzero(mask))
    require(checked > 1000000, 'insufficient independent grid/background samples')
    return checked


def check_record(record, token, identity, artifact, level, read):
    require(record.get('schema_version') == 1 and record.get('status') == 'pass' and record.get('run_token') == token, 'stale or failed record')
    require(record.get('required') == record.get('executed') == 29 and len(record.get('cases', [])) == 29, 'incomplete cases')
    require(record.get('host_source_sha256') == identity and record.get('shader') == artifact and record.get('optimization_level') == level, 'wrong source identity')
    images, previous, before = {}, [0,0,0,0], bytes(112)
    for index, (step, row) in enumerate(zip(inputs(), record['cases'])):
        name, *source_mouse = step
        # serde_json records f32 inputs as their exact promoted f64 values.
        mouse = np.asarray(source_mouse, dtype=np.float32).astype(float).tolist()
        case_id = f's2h.zoom.{name}.opt{level}'
        require(row.get('case_id') == case_id and row.get('status') == 'pass', 'wrong case identity')
        require(row.get('mouse') == mouse and row.get('previous_mouse') == previous
                and row.get('root_sha256') == sha256(root_bytes(mouse, previous)), 'wrong input root')
        require(row.get('before_state_sha256') == sha256(before), 'broken state chain')
        require(row.get('completion_values') == list(range(3+index*6,9+index*6)), 'wrong pre/render/post order')
        pre, post = expected_states(index)
        for key, extension, size, expected in (('pre','pre',112,pre),('post','post',112,post),('image','rgba8',1920000,None)):
            name_on_disk = case_id + '.' + extension
            data = read(name_on_disk)
            require(len(data) == size and row.get(key) == dict(output=name_on_disk, bytes=size, sha256=sha256(data)), 'incomplete or stale ' + key)
            if expected is not None:
                require(data == expected, f'{name}: incorrect {key} state, actual words {list(struct.unpack("<28I",data))}')
            else: images[name] = np.frombuffer(data, dtype=np.uint8).reshape(600,800,4)
        previous, before = mouse, post
    device = record.get('device', {})
    require(device.get('validation') is True and device.get('synchronization_validation') is True
            and min(device.get('api_version',0),device.get('loader_api_version',0)) >= (1<<22|4<<12), 'validation or Vulkan 1.4 missing')
    return structural(images)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable','sdk','shader-dir','output-dir','review'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id='s2h.zoom.execution', status='fail', run_token=token,
                  required=58, executed=0, source_image_agreement='not checked', runs=[], runner_sha256=sha256(Path(__file__).read_bytes()))
    try:
        exe,sdk,shaders,review_path = (p.resolve(strict=True) for p in (args.executable,args.sdk,args.shader_dir,args.review))
        identity = {name:sha256((ROOT/name).read_bytes()) for name in SOURCES}
        review = json.loads(review_path.read_text(encoding='utf-8'))
        report['executable_sha256'] = sha256(exe.read_bytes())
        require(review.get('status') == 'reviewed-before-dispatch' and review.get('host_sources') == identity
                and review.get('executable_sha256') == report['executable_sha256'], 'missing or stale pre-execution review')
        report['review_sha256'] = sha256(review_path.read_bytes())
        compiled = subprocess.run([str(exe),'--identity'],capture_output=True,check=True,timeout=10)
        require(json.loads(compiled.stdout) == identity, 'stale executable rejected before Vulkan')
        environment = os.environ.copy()
        for name in ('VK_LAYER_ENABLES','VK_LAYER_DISABLES','VK_LAYER_SETTINGS_PATH'): environment.pop(name,None)
        environment.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(sdk/'Bin'),VK_LOADER_DEBUG='layer',VK_LAYER_VALIDATE_CORE='1',VK_LAYER_VALIDATE_SYNC='1')
        environment['PATH'] = str(sdk/'Bin') + os.pathsep + environment['PATH']
        report['environment'] = {k:v for k,v in environment.items() if k.startswith('VK_')}
        for level in (0,3):
            artifact = json.loads((shaders/f'zoom_opt{level}.metadata.json').read_text(encoding='utf-8'))
            require(artifact['payload_sha256'] == sha256((shaders/f'zoom_opt{level}.spv').read_bytes())
                    == review.get('payloads',{}).get(str(level)), 'unreviewed or stale payload')
            folder=output/f'opt{level}';folder.mkdir(exist_ok=True)
            for name in NAMES:
                for ext in ('pre','post','rgba8'): (folder/f's2h.zoom.{name}.opt{level}.{ext}').unlink(missing_ok=True)
            command=[str(exe),'--run-token',token,'--shader-dir',str(shaders),'--level',str(level)]
            log.event('native_started',level=level,command=command); report['executed']=None
            try: result=subprocess.run(command,cwd=folder,env=environment,capture_output=True,timeout=120)
            except subprocess.TimeoutExpired as error:
                (folder/'native.stdout.txt').write_bytes(error.stdout or b''); (folder/'native.stderr.txt').write_bytes(error.stderr or b''); raise
            (folder/'native.stdout.txt').write_bytes(result.stdout); (folder/'native.stderr.txt').write_bytes(result.stderr)
            stdout,stderr=result.stdout.decode('utf-8','replace'),result.stderr.decode('utf-8','replace')
            diagnostics,notices=validation_messages(stdout+stderr)
            inserted='Insert instance layer "VK_LAYER_KHRONOS_validation"' in stderr and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in stderr
            require(result.returncode==0 and inserted and not diagnostics, f'opt{level}: native/validation failure {diagnostics[:2]}')
            native=json.loads(stdout)
            checked=check_record(native,token,identity,artifact,level,lambda name:(folder/name).read_bytes())
            report['runs'].append(dict(level=level,native=native,grid_background_samples=checked,validation=dict(inserted=inserted,diagnostics=diagnostics,notices=notices)))
            report['executed']=29*len(report['runs'])
            print(json.dumps(dict(level=level,status='pass',executed=29)),flush=True)
        require(sha256(exe.read_bytes())==report['executable_sha256'], 'executable changed during run')
        report['status']='pass'
    except (OSError,ValueError,KeyError,TypeError,AssertionError,subprocess.SubprocessError) as error:
        report['error']=str(error);log.event('failed',error=str(error))
    finally:
        report['elapsed_seconds']=time.monotonic()-start;log.finish(report)
    print(json.dumps({k:report.get(k) for k in ('case_id','status','required','executed','error')}))
    return report['status']!='pass'


if __name__ == '__main__':
    raise SystemExit(main())
