"""Run original AGFX raster goldens plus independent coverage, alpha and depth checks."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import time
import uuid

from run_agfx_copy import ROOT, require, sha256, validation_messages
from run_log import RunLog
import source_flip

SOURCES = ['Cargo.toml','Cargo.lock','crates/agfx/Cargo.toml','crates/agfx/src/lib.rs',
    'crates/agfx/src/vulkan.rs','crates/agfx/src/vulkan/bindings.rs','crates/agfx/src/vulkan/compute.rs',
    'crates/agfx/src/vulkan/graphics.rs','crates/agfx/src/vulkan/ownership.rs',
    'crates/agfx/src/vulkan/sampler.rs','crates/agfx/src/vulkan/texture.rs',
    'crates/agfx/src/bin/raster.rs','shaders/rust/raster.rs']
COMPARES = ['never','less','equal','less_equal','greater','not_equal','greater_equal','always']
FACTORS = ['zero','one','src_color','one_minus_src_color','dst_color','one_minus_dst_color',
           'src_alpha','one_minus_src_alpha','dst_alpha','one_minus_dst_alpha']
OPERATIONS = ['add','subtract','reverse_subtract','min','max']
NAMES = ['pipeline_cull_mode_none','pipeline_cull_mode_back','pipeline_cull_mode_front','draw_ccw','draw_cw',
    'draw_wireframe','draw_points','draw_lines','draw_fragment_discard','draw_push_constants',
    'draw_triangle','viewport','scissor_rect','cache_roundtrip','draw_indexed','draw_indexed_u16','draw_lines_indexed','indexed_offset']
NAMES += ['draw_depth_test_'+n for n in COMPARES] + ['draw_depth_clear_value']
NAMES += ['draw_depth_clamp_disabled','depth_write_disabled','draw_depth_clamp_enabled','depth_write_enabled']
NAMES += ['draw_blend_factor_'+n for n in FACTORS] + ['draw_blend_op_'+n for n in OPERATIONS] + ['draw_alpha_blend']
NAMES += ['render_pass_action_clear','render_pass_action_load','render_pass_action_dont_care']
GOLDENS = {n: {'cache_roundtrip':'draw_triangle','draw_indexed_u16':'draw_indexed','indexed_offset':'draw_indexed'}.get(n,n)+'.png' for n in NAMES}
CONTROLS = ['color_clear_on_depth','depth_clear_on_color','nonfinite_depth_clear','depth_transfer_clear',
    'no_attachments','uninitialized_load','foreign_attachment','missing_attachment_usage','attachment_extent',
    'color_as_depth','depth_as_color','invalid_pass_depth_clear','discarded_store_load','discarded_store_readback',
    'root_length','viewport_nan','scissor_negative','index_range','index_uninitialized','foreign_index']


def column_at(x,y):
    if 32 <= y < 96:
        for i,(left,right) in enumerate(((10,42),(48,80),(86,118))):
            if left <= x < right: return i
    return None


def depth_expected(name,x,y):
    column = column_at(x,y)
    if name.startswith('draw_depth_test_'): return 0.5
    if name == 'draw_depth_clear_value': return 0.5
    if name == 'depth_write_enabled' and column is not None: return 0.5
    return 1.0


def depth_color(name,x,y):
    column = column_at(x,y)
    active = set()
    background = 0.0
    if name.startswith('draw_depth_test_'):
        active = [set(),{0},{1},{0,1},{2},{0,2},{1,2},{0,1,2}][COMPARES.index(name.removeprefix('draw_depth_test_'))]
    elif name == 'draw_depth_clear_value': active = {0}
    elif name == 'draw_depth_clamp_disabled': active = {1}
    elif name == 'draw_depth_clamp_enabled': active = {0,1,2}
    elif name.startswith('depth_write_'):
        background = 0.5
        active = {0,1,2} if name == 'depth_write_enabled' else set()
    if column is not None and column in active: return [float(c == column) for c in range(3)] + [1.0]
    return [background]*3+[1.0]


def blend_expected(name,x,y):
    column = column_at(x,y)
    dst = [0.0]*4
    if column is not None:
        dst[column] = 102/255
        dst[3] = [1.0,153/255,51/255][column]
    src = [0.5,0.25,0.125,0.25]
    sf,df,op = 'one','one','add'
    if name.startswith('draw_blend_factor_'): sf = name.removeprefix('draw_blend_factor_')
    elif name.startswith('draw_blend_op_'): op = name.removeprefix('draw_blend_op_')
    else: sf,df = 'src_alpha','one_minus_src_alpha'
    def factor(kind,c):
        if kind.startswith('one_minus_'): return 1-factor(kind.removeprefix('one_minus_'),c)
        return {'zero':0,'one':1,'src_color':src[c],'dst_color':dst[c],'src_alpha':src[3],'dst_alpha':dst[3]}[kind]
    result = []
    for c in range(4):
        a,b = src[c]*factor(sf,c),dst[c]*factor(df,c)
        value = {'add':a+b,'subtract':a-b,'reverse_subtract':b-a,'min':min(src[c],dst[c]),'max':max(src[c],dst[c])}[op]
        result.append(min(1,max(0,value))*255)
    return result


def structural(name,actual,depth):
    require(len(actual) == 128*128*4, 'incomplete raster image')
    pixels = [actual[i:i+4] for i in range(0,len(actual),4)]
    lit = sum(any(p[:3]) for p in pixels)
    first = None
    is_depth = name.startswith('draw_depth_') or name.startswith('depth_write_')
    is_blend = name.startswith('draw_blend_') or name == 'draw_alpha_blend'
    for y in range(128):
        for x in range(128):
            pixel = pixels[y*128+x]
            reason = None
            if is_blend:
                expected = blend_expected(name,x,y)
                if any(abs(a-e)>1.0001 for a,e in zip(pixel,expected)): reason = ['blend arithmetic',list(pixel),expected]
            elif is_depth:
                expected = depth_color(name,x,y)
                if any(not math.floor(e*255) <= a <= math.ceil(e*255) for a,e in zip(pixel,expected)): reason = ['depth coverage/color',list(pixel),expected]
            elif pixel[3] != 255: reason = ['opaque alpha',pixel[3],255]
            if name == 'viewport' and (x<64 or y<64) and any(pixel[:3]): reason = ['outside viewport',list(pixel)]
            if name == 'scissor_rect' and not(24<=x<88 and 40<=y<104) and any(pixel[:3]): reason = ['outside scissor',list(pixel)]
            if name == 'draw_fragment_discard' and x>=96 and any(pixel[:3]): reason = ['discarded region',list(pixel)]
            if name == 'draw_push_constants' and any(pixel[:3]):
                if any(not lo<=a<=hi for a,lo,hi in zip(pixel,[63,191,127,255],[64,192,128,255])): reason = ['push color',list(pixel)]
            if name.startswith('render_pass_action_'):
                expected = None
                if name.endswith('dont_care') or (x,y)==(64,64): expected = bytes([255,128,0,255])
                elif x<16 and y<16:
                    expected = bytes([51,102,204,255]) if name.endswith('clear') else bytes([x,y,x^y,255])
                if expected is not None and pixel != expected: reason = ['load/store region',list(pixel),list(expected)]
            if first is None and reason is not None: first = dict(x=x,y=y,reason=reason)
    sparse_ok = (lit == 6 if name == 'draw_points' else
        lit>150 if name == 'draw_lines' else lit>400 if name == 'draw_lines_indexed' else
        100<lit<1500 if name == 'draw_wireframe' else True)
    if not sparse_ok and first is None: first = dict(reason=['sparse coverage',lit])
    depth_first = None
    if is_depth:
        require(depth is not None and len(depth)==65536, 'missing complete depth readback')
        for i,value in enumerate(struct.unpack('<16384f',depth)):
            expected = depth_expected(name,i%128,i//128)
            if not math.isfinite(value) or abs(value-expected)>1e-6:
                depth_first = dict(x=i%128,y=i//128,actual=value,expected=expected); break
    else: require(depth is None, 'unexpected depth artifact')
    return dict(status='pass' if first is None and depth_first is None else 'fail',lit_pixels=lit,
                first_difference=first,depth_first_difference=depth_first)


def expected_outputs():
    from PIL import Image
    manifest = json.loads((ROOT/'tests/parity/raster-sources.json').read_text())
    goldens = {}
    for row in manifest['goldens']:
        path = ROOT/row['path']
        require(sha256(path.read_bytes()) == row['sha256'], 'changed original raster golden')
        with Image.open(path) as image:
            require(image.size==(128,128) and image.mode=='RGBA', 'wrong raster golden shape')
            goldens[path.name] = image.tobytes()
    return {name:goldens[GOLDENS[name]] for name in NAMES}


def check_record(record,token,identity,artifacts,outputs,depths):
    require(record.get('schema_version')==1 and record.get('run_token')==token and record.get('status')=='executed', 'native run identity/status mismatch')
    require(record.get('host_source_sha256')==identity and record.get('shaders')==artifacts, 'native compiled identities differ')
    require(record.get('required')==record.get('executed')==100, 'wrong raster denominator')
    ids = [f'agfx.raster.{name}.opt{level}' for level in (0,3) for name in NAMES]
    require([r.get('case_id') for r in record.get('cases',[])]==ids and set(outputs)==set(ids), 'missing, duplicate or reordered raster cases')
    controls = record.get('controls',[])
    require([r.get('case_id') for r in controls]==CONTROLS and all(r.get('status')=='pass' for r in controls), 'missing or failed native controls')
    require(controls[3]['sha256']==sha256(struct.pack('<f',.375)*16384), 'depth clear control differs')
    device = record.get('device',{})
    require(device.get('validation') is True and device.get('synchronization_validation') is True, 'validation was not requested')
    require(min(device.get('api_version',0),device.get('loader_api_version',0)) >= (1<<22|4<<12), 'Vulkan 1.4 was not selected')
    require(all(device.get('graphics',{}).get(n) is True for n in ('dynamic_rendering','wireframe','depth_clamp')), 'required raster capabilities unavailable')
    previous = controls[3]['completion_values'][-1]
    for row in record['cases']:
        id = row['case_id']; name = id.removeprefix('agfx.raster.').rsplit('.opt',1)[0]
        require(row.get('status')=='executed' and row.get('bytes')==65536 and sha256(outputs[id])==row.get('sha256'), 'missing or altered raster bytes')
        require(row.get('golden')==GOLDENS[name], 'wrong source golden mapping')
        completed = row.get('completion_values',[])
        require(len(completed)==2 and previous<completed[0]<completed[1], 'missing ordered completion')
        previous = completed[1]
        require(bool(row.get('cache_sizes')) == (name=='cache_roundtrip') and all(n>=32 for n in row.get('cache_sizes',[])), 'cache roundtrip missing')
        require(len(row.get('draws',[])) in (1,2) and all(d.get('count',0)>0 for d in row['draws']), 'zero draw work')
        if row.get('depth') is not None:
            d = row['depth']; require(d['completion']>previous and d['bytes']==65536 and d['sha256']==sha256(depths[id]), 'depth readback identity mismatch')
            previous = d['completion']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('executable','sdk','shader-dir','output-dir','flip-reference'):
        parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args(); output = args.output_dir.resolve(); output.mkdir(parents=True,exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token,started = str(uuid.uuid4()),time.monotonic(); log = RunLog(output,token)
    report = dict(schema_version=1,case_id='agfx.raster.run',run_token=token,status='fail',required=100,executed=0,passed=0,
                  runner_sha256=sha256(Path(__file__).read_bytes()),oracle_source_sha256=sha256(Path(source_flip.__file__).read_bytes()),
                  validation_parser_sha256=sha256(Path(__file__).with_name('run_agfx_copy.py').read_bytes()))
    try:
        exe,sdk,shaders = [p.resolve(strict=True) for p in (args.executable,args.sdk,args.shader_dir)]
        identity = {p:sha256((ROOT/p).read_bytes()) for p in SOURCES}; expected = expected_outputs()
        report['flip_identity'] = source_flip.identity(args.flip_reference)
        artifacts = [json.loads((shaders/f'raster_opt{level}.metadata.json').read_text()) for level in (0,3)]
        for level,a in zip((0,3),artifacts):
            require(sha256((shaders/f'raster_opt{level}.spv').read_bytes())==a['payload_sha256'], 'changed compiled shader')
        built = subprocess.run([str(exe),'--identity'],capture_output=True,check=True,timeout=10)
        require(json.loads(built.stdout)==identity,'stale executable rejected before Vulkan')
        report.update(host_source_sha256=identity,executable_sha256=sha256(exe.read_bytes()),
                      validation_layer_sha256=sha256((sdk/'Bin/VkLayer_khronos_validation.dll').read_bytes()))
        env = os.environ.copy()
        for key in ('VK_LAYER_ENABLES','VK_LAYER_DISABLES','VK_LAYER_SETTINGS_PATH'): env.pop(key,None)
        env.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(sdk/'Bin'),VK_LOADER_DEBUG='layer',VK_LAYER_VALIDATE_CORE='1',VK_LAYER_VALIDATE_SYNC='1')
        env['PATH'] = str(sdk/'Bin')+os.pathsep+env['PATH']
        command = [str(exe),'--shader-dir',str(shaders),'--run-token',token]
        report.update(command=command,environment={k:v for k,v in env.items() if k.startswith('VK_')})
        log.event('native_started',command=command)
        result = subprocess.run(command,cwd=output,env=env,capture_output=True,timeout=90)
        (output/'native.stdout.txt').write_bytes(result.stdout); (output/'native.stderr.txt').write_bytes(result.stderr)
        stdout,stderr = result.stdout.decode('utf-8','replace'),result.stderr.decode('utf-8','replace')
        diagnostics,notices = validation_messages(stdout+stderr)
        inserted = 'Insert instance layer "VK_LAYER_KHRONOS_validation"' in stderr and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in stderr
        report.update(exit_code=result.returncode,validation_inserted=inserted,validation_diagnostics=diagnostics,intentional_loader_notices=notices)
        require(result.returncode==0 and inserted and not diagnostics,'native exit or validation failed')
        record = json.loads(stdout)
        outputs = {r['case_id']:(output/(r['case_id']+'.rgba8')).read_bytes() for r in record['cases']}
        depths = {r['case_id']:(output/(r['case_id']+'.depth32')).read_bytes() for r in record['cases'] if r.get('depth') is not None}
        check_record(record,token,identity,artifacts,outputs,depths)
        report.update(executed=100,execution_passed=100,native=record)
        comparisons = []
        for row in record['cases']:
            id = row['case_id']; name = id.removeprefix('agfx.raster.').rsplit('.opt',1)[0]
            actual,reference = outputs[id],expected[name]
            diff = [abs(a-b) for a,b in zip(actual,reference)]
            flip = source_flip.evaluate(args.flip_reference,output,id,reference,actual,128,128)
            controls = structural(name,actual,depths.get(id))
            comparisons.append(dict(case_id=id,status='pass' if source_flip.passed(flip['mean']) and controls['status']=='pass' else 'fail',
                exact_status='pass' if not any(diff) else 'fail',different_channels=sum(d!=0 for d in diff),max_channel_error=max(diff),
                flip=flip,structural=controls))
        passed = sum(r['status']=='pass' for r in comparisons)
        report.update(passed=passed,status='pass' if passed==100 else 'fail',comparisons=comparisons)
    except subprocess.TimeoutExpired as error:
        (output/'native.stdout.txt').write_bytes(error.stdout or b''); (output/'native.stderr.txt').write_bytes(error.stderr or b'')
        report.update(failure='native timeout',timed_out=True)
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error: report['failure']=str(error)
    except KeyboardInterrupt: report.update(status='incomplete',failure='interrupted')
    report['elapsed_seconds']=time.monotonic()-started; log.finish(report)
    print(json.dumps({k:report.get(k) for k in ('case_id','status','failure','required','executed','passed','validation_inserted','elapsed_seconds')}))
    return report['status']!='pass'


if __name__=='__main__': raise SystemExit(main())
