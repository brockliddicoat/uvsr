"""Run source Hello examples with explicit color, camera and persistent-frame checks."""
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

NAMES = ('screen', 'compute', 'quad_source', 'quad_motion', 'quad_frontal', 'quad_moved')
SOURCES = ['Cargo.toml', 'Cargo.lock', 'crates/agfx/Cargo.toml', 'crates/agfx/src/lib.rs',
           'crates/agfx/src/vulkan.rs', 'crates/shader-to-human/Cargo.toml',
           'crates/shader-to-human/examples/hello.rs', 'crates/shader-to-human/programs/hello.rs',
           'shaders/rust/shader_to_human_hello.rs', 'tests/parity/fixtures/shader-to-human/hello-cameras.txt']
SOURCES += ['crates/agfx/src/vulkan/' + name + '.rs' for name in
            ('compute', 'bindings', 'graphics', 'ownership', 'sampler', 'texture')]
SOURCES += ['crates/shader-to-human/src/' + name + '.rs' for name in
            ('lib', 'font', 'math', 'gather', 'widgets', 'scatter', 'world')]


def cameras():
    rows = [[float(v) for v in line.split()] for line in
            (ROOT / 'tests/parity/fixtures/shader-to-human/hello-cameras.txt').read_text().splitlines()]
    require(len(rows) == 3 and all(len(r) == 24 and r[20:] == [800, 600, 0, 0] for r in rows), 'invalid cameras')
    return np.array(rows, dtype=np.float32)


def srgb(value):
    value = np.maximum(value, 0)
    return np.where(value <= .0031308, value * 12.92, 1.055 * value ** (1 / 2.4) - .055)


def close_codes(actual, expected, label):
    difference = np.abs(np.asarray(actual, dtype=np.float64) - np.clip(expected, 0, 1) * 255)
    require(np.all(np.isfinite(difference)), label + ': nonfinite comparison')
    bad = np.argwhere(difference > 1.01)
    require(not len(bad), f'{label}: first channel {bad[0].tolist() if len(bad) else None}')


def coordinates(camera):
    matrix = camera[:16].reshape(4, 4).T.astype(np.float64)
    y, x = np.mgrid[:600, :800]
    ndc = np.stack(((x + .5) / 400 - 1, 1 - (y + .5) / 300, np.ones_like(x)), axis=-1)
    homography = matrix[[0, 1, 3]][:, [0, 1, 3]]
    obj_h = ndc @ np.linalg.inv(homography).T
    obj = obj_h[..., :2] / obj_h[..., 2:3]
    uv = (obj - 1) / 2
    uv[..., 1] = 1 - uv[..., 1]
    clip = np.concatenate((obj, np.zeros((600, 800, 1)), np.ones((600, 800, 1))), axis=-1) @ matrix.T
    return uv, clip[..., 2] / clip[..., 3], clip[..., 3]


def structural(images, rows):
    y, x = np.mgrid[:600, :800]
    for name in ('screen', 'compute'):
        data = images[name]
        require(np.all(data[..., 3] == 255), name + ': nonopaque output')
        background = np.stack(((x + .5) / 800, (y + .5) / 600, np.zeros_like(x)), axis=-1)
        if name == 'screen': background = srgb(background)
        mask = y >= 136
        close_codes(data[..., :3][mask], background[mask], name + ' background')
        white = np.all(data[10:58, 10:178, :3] == 255, axis=-1)
        require(np.count_nonzero(white) > 1300, name + ': missing source text')
    # Source ramp has continuous and sixteen-level rows, before its labels.
    for row, quantized in ((110, False), (120, True)):
        ramp = np.arange(1, 256) / 256
        if quantized: ramp = np.floor(ramp * 16) / 16
        close_codes(images['screen'][row, 11:266, :3], ramp[:, None], 'screen ramp')
    for name, index in (('quad_source', 0), ('quad_motion', 2), ('quad_frontal', 1), ('quad_moved', 2)):
        uv, z, w = coordinates(rows[index])
        inside = np.all((uv > .005) & (uv < .995), axis=-1)
        outside = np.any((uv < -.005) | (uv > 1.005), axis=-1)
        if name == 'quad_motion':
            old_uv, _, _ = coordinates(rows[0])
            inside |= np.all((old_uv > .005) & (old_uv < .995), axis=-1)
            outside &= np.any((old_uv < -.005) | (old_uv > 1.005), axis=-1)
        data = images[name]
        require(np.count_nonzero(inside) > 20000, name + ': degenerate expected geometry')
        require(np.all(data[inside, 3] == 255), name + ': missing covered alpha')
        require(np.all(data[outside] == 0), name + ': changed untouched pixels')
        px = uv * 256
        for left, right, value, label in ((26, 112, z, 'depth'), (146, 230, w - np.floor(w), 'clip w')):
            # Below the text in each source rectangle, away from all edges.
            mask = (px[..., 0] > left) & (px[..., 0] < right) & (px[..., 1] > 203) & (px[..., 1] < 211)
            require(np.count_nonzero(mask) > 100, name + ': missing numeric rectangle')
            close_codes(data[..., :3][mask], srgb(value[mask])[:, None], name + ' ' + label)
    # A moved load must retain the old silhouette. A fresh moved frame does not.
    old = images['quad_source'][..., 3] == 255
    moved = images['quad_moved'][..., 3] == 255
    retained = old & ~moved
    require(np.count_nonzero(retained) > 1000, 'motion did not expose retained pixels')
    require(np.array_equal(images['quad_motion'][retained], images['quad_source'][retained]), 'prior frame was cleared')


def check_record(record, token, identity, artifact, level, read):
    require(record.get('schema_version') == 1 and record.get('status') == 'pass' and record.get('run_token') == token, 'stale or failed record')
    require(record.get('required') == record.get('executed') == 6 and len(record.get('cases', [])) == 6, 'incomplete cases')
    require(record.get('host_source_sha256') == identity and record.get('shader') == artifact and record.get('optimization_level') == level, 'wrong source identity')
    rows = cameras()
    images = {}
    for name, row, index, complete in zip(NAMES, record['cases'], (0, 0, 0, 2, 1, 2), ((2,3),(5,6),(8,9),(10,11),(13,14),(16,17))):
        case_id = f's2h.hello.{name}.opt{level}'
        require(row.get('case_id') == case_id and row.get('output') == case_id + '.rgba8' and row.get('status') == 'pass', 'wrong case identity')
        require(row.get('root_sha256') == sha256(struct.pack('<24f', *rows[index])), 'wrong source camera root')
        require(row.get('completion_values') == list(complete), 'wrong ordered completion')
        data = read(row['output'])
        require(row.get('bytes') == len(data) == 800*600*4 and row.get('sha256') == sha256(data), 'incomplete or stale image')
        images[name] = np.frombuffer(data, dtype=np.uint8).reshape(600, 800, 4)
    device = record.get('device', {})
    require(device.get('validation') is True and device.get('synchronization_validation') is True and device.get('api_version', 0) >= (1 << 22 | 4 << 12), 'validation or Vulkan 1.4 missing')
    structural(images, rows)
    return images


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable', 'sdk', 'shader-dir', 'output-dir'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token, start = str(uuid.uuid4()), time.monotonic()
    log = RunLog(output, token)
    report = dict(schema_version=1, case_id='s2h.hello.execution', status='fail', run_token=token, required=12, executed=0,
                  source_image_agreement='not checked', runs=[], runner_sha256=sha256(Path(__file__).read_bytes()))
    try:
        exe, sdk, shaders = (p.resolve(strict=True) for p in (args.executable, args.sdk, args.shader_dir))
        identity = {name:sha256((ROOT/name).read_bytes()) for name in SOURCES}
        report['executable_sha256'] = sha256(exe.read_bytes())
        compiled = subprocess.run([str(exe), '--identity'], capture_output=True, check=True, timeout=10)
        require(json.loads(compiled.stdout) == identity, 'stale executable rejected before Vulkan')
        environment = os.environ.copy()
        for name in ('VK_LAYER_ENABLES','VK_LAYER_DISABLES','VK_LAYER_SETTINGS_PATH'): environment.pop(name,None)
        environment.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(sdk/'Bin'),VK_LOADER_DEBUG='layer',VK_LAYER_VALIDATE_CORE='1',VK_LAYER_VALIDATE_SYNC='1')
        environment['PATH'] = str(sdk/'Bin') + os.pathsep + environment['PATH']
        report['environment'] = {k:v for k,v in environment.items() if k.startswith('VK_')}
        for level in (0,3):
            artifact = json.loads((shaders/f'hello_opt{level}.metadata.json').read_text())
            require(artifact['payload_sha256'] == sha256((shaders/f'hello_opt{level}.spv').read_bytes()), 'stale payload')
            folder=output/f'opt{level}';folder.mkdir(exist_ok=True)
            for name in NAMES: (folder/f's2h.hello.{name}.opt{level}.rgba8').unlink(missing_ok=True)
            command=[str(exe),'--run-token',token,'--shader-dir',str(shaders),'--level',str(level)]
            log.event('native_started',level=level,command=command);report['executed']=None
            try: result=subprocess.run(command,cwd=folder,env=environment,capture_output=True,timeout=90)
            except subprocess.TimeoutExpired as error:
                (folder/'native.stdout.txt').write_bytes(error.stdout or b'');(folder/'native.stderr.txt').write_bytes(error.stderr or b'');raise
            (folder/'native.stdout.txt').write_bytes(result.stdout);(folder/'native.stderr.txt').write_bytes(result.stderr)
            stdout,stderr=result.stdout.decode('utf-8','replace'),result.stderr.decode('utf-8','replace')
            diagnostics,notices=validation_messages(stdout+stderr)
            inserted='Insert instance layer "VK_LAYER_KHRONOS_validation"' in stderr and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in stderr
            require(result.returncode==0 and inserted and not diagnostics, f'opt{level}: native/validation failure {diagnostics[:2]}')
            native=json.loads(stdout)
            check_record(native,token,identity,artifact,level,lambda name:(folder/name).read_bytes())
            report['runs'].append(dict(level=level,native=native,validation=dict(inserted=inserted,diagnostics=diagnostics,notices=notices)))
            report['executed']=6*len(report['runs'])
            print(json.dumps(dict(level=level,status='pass',executed=6)),flush=True)
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
