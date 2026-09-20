"""Verify UNORM writes, sRGB reads/attachments, and common texture ownership."""
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

SOURCES=['Cargo.toml','Cargo.lock','crates/agfx/Cargo.toml','crates/agfx/src/lib.rs','crates/agfx/src/vulkan.rs',
         'crates/agfx/src/bin/texture_views.rs','shaders/rust/texture_views.rs']
SOURCES+=['crates/agfx/src/vulkan/'+name+'.rs' for name in ('compute','bindings','graphics','ownership','sampler','texture')]
CONFIGS=('unorm','srgb_views','srgb_base')
CONTROLS=['incompatible_sampled','incompatible_storage','incompatible_attachment','empty_usage','depth_storage']
CONTROLS += [name+'_attachment_format' for name in CONFIGS]


def check_pixels(encoded,sampled,storage,srgb_view,srgb_write):
    expected=np.array([124,170,203,204] if srgb_write else [51,102,153,204],dtype=np.uint8)
    pixels=np.frombuffer(encoded,dtype=np.uint8).reshape(64,4)
    require(np.all(pixels==expected),'encoded view bytes differ')
    errors={}
    for name,data,decode in [('sampled',sampled,srgb_view),('storage',storage,False)]:
        actual=np.frombuffer(data,dtype='<f4').reshape(64,4)
        require(np.isfinite(actual).all(),name+': nonfinite readback')
        wanted=expected.astype(np.float64)/255
        if decode:
            rgb=wanted[:3]
            wanted[:3]=np.where(rgb<=.04045,rgb/12.92,((rgb+.055)/1.055)**2.4)
            # Test preservation of the source's eight-bit color information.
            # Hardware conversion need not match a CPU pow bit for bit. Apply
            # Khronos' inverse EOTF independently and require the exact original
            # code at every texel. This is not a Vulkan precision conformance test.
            rgb=actual[:,:3].astype(np.float64)
            require(np.all((rgb>=0)&(rgb<=1)),name+': decoded value outside [0,1]')
            encoded_rgb=np.where(rgb<=.0031308,12.92*rgb,1.055*rgb**(1/2.4)-.055)
            require(np.all(np.floor(encoded_rgb*255+.5)==expected[:3]),name+': view conversion roundtrip differs')
        error=np.abs(actual-wanted)
        errors[name]=float(error.max())
        # Alpha is always linear. All four UNORM components stay strictly checked.
        bad=np.argwhere((error[:,3:] if decode else error)>1e-7)
        require(not len(bad),f'{name}: view conversion differs at {bad[0].tolist() if len(bad) else None}, max={float(error.max())}')
    return errors


def check_record(record,token,identity,artifact,level,read):
    require(record.get('schema_version')==1 and record.get('status')=='pass' and record.get('run_token')==token,'stale or failed run')
    require(record.get('required')==record.get('executed')==6 and len(record.get('cases',[]))==6,'incomplete cases')
    require(record.get('optimization_level')==level and record.get('host_source_sha256')==identity and record.get('shader')==artifact,'stale source or shader')
    require([r.get('case_id') for r in record.get('controls',[])]==CONTROLS
            and all(r.get('status')=='pass' and r.get('diagnostic') for r in record['controls']),'missing validation controls')
    errors=[]
    for index,row in enumerate(record['cases']):
        name,phase=CONFIGS[index//2],('storage','raster')[index%2]
        case_id=f'agfx.views.{name}.{phase}.opt{level}'
        require(row.get('case_id')==case_id and row.get('status')=='pass','wrong case identity')
        require(row.get('base_format')==('Rgba8Srgb' if name=='srgb_base' else 'Rgba8Unorm')
                and row.get('view_format')==('Rgba8Unorm' if name=='unorm' else 'Rgba8Srgb'),'wrong format identity')
        require(row.get('root_sha256')==sha256(struct.pack('<4f',.2,.4,.6,.8)),'wrong shader root')
        start=(3,9,16,22,29,35)[index]
        require(row.get('completion_values')==list(range(start,start+6)),'wrong ordered completion')
        buffers=[]
        for key,extension,size in [('encoded','rgba8',256),('sampled','sampled.f32',1024),('storage','storage.f32',1024)]:
            filename=case_id+'.'+extension;data=read(filename)
            require(len(data)==size and row.get(key)==dict(output=filename,bytes=size,sha256=sha256(data)),'incomplete or stale '+key)
            buffers.append(data)
        errors.append(dict(case_id=case_id,max_absolute_error=check_pixels(*buffers,name!='unorm',phase=='raster' and name!='unorm')))
    device=record.get('device',{})
    require(device.get('validation') is True and device.get('synchronization_validation') is True
            and device.get('api_version',0)>=(1<<22|4<<12),'validation or Vulkan 1.4 missing')
    return errors


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('executable','sdk','shader-dir','output-dir','review'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token,start=str(uuid.uuid4()),time.monotonic();log=RunLog(output,token)
    report=dict(schema_version=1,case_id='agfx.views.execution',status='fail',run_token=token,required=12,executed=0,runs=[],runner_sha256=sha256(Path(__file__).read_bytes()))
    try:
        exe,sdk,shaders,review_path=(p.resolve(strict=True) for p in (args.executable,args.sdk,args.shader_dir,args.review))
        identity={p:sha256((ROOT/p).read_bytes()) for p in SOURCES};exe_hash=sha256(exe.read_bytes())
        review=json.loads(review_path.read_text(encoding='utf-8'))
        require(review.get('status')=='reviewed-before-dispatch' and review.get('host_sources')==identity and review.get('executable_sha256')==exe_hash,'missing or stale pre-execution review')
        require(json.loads(subprocess.check_output([str(exe),'--identity'],timeout=10))==identity,'stale host rejected before Vulkan')
        report.update(executable_sha256=exe_hash,review_sha256=sha256(review_path.read_bytes()))
        env=os.environ.copy()
        for name in ('VK_LAYER_ENABLES','VK_LAYER_DISABLES','VK_LAYER_SETTINGS_PATH'):env.pop(name,None)
        env.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(sdk/'Bin'),VK_LOADER_DEBUG='layer',VK_LAYER_VALIDATE_CORE='1',VK_LAYER_VALIDATE_SYNC='1')
        env['PATH']=str(sdk/'Bin')+os.pathsep+env['PATH'];report['environment']={k:v for k,v in env.items() if k.startswith('VK_')}
        for level in (0,3):
            artifact=json.loads((shaders/f'texture_views_opt{level}.metadata.json').read_text(encoding='utf-8'))
            require(artifact['payload_sha256']==sha256((shaders/f'texture_views_opt{level}.spv').read_bytes())==review.get('payloads',{}).get(str(level)),'stale or unreviewed payload')
            folder=output/f'opt{level}';folder.mkdir(exist_ok=True)
            for name in CONFIGS:
                for phase in ('storage','raster'):
                    for ext in ('rgba8','sampled.f32','storage.f32'):(folder/f'agfx.views.{name}.{phase}.opt{level}.{ext}').unlink(missing_ok=True)
            command=[str(exe),'--run-token',token,'--shader-dir',str(shaders),'--level',str(level)]
            report['executed']=None;log.event('native_started',level=level,command=command)
            try:result=subprocess.run(command,cwd=folder,env=env,capture_output=True,timeout=60)
            except subprocess.TimeoutExpired as error:
                (folder/'native.stdout.txt').write_bytes(error.stdout or b'');(folder/'native.stderr.txt').write_bytes(error.stderr or b'');raise
            (folder/'native.stdout.txt').write_bytes(result.stdout);(folder/'native.stderr.txt').write_bytes(result.stderr)
            stdout,stderr=result.stdout.decode('utf-8','replace'),result.stderr.decode('utf-8','replace')
            diagnostics,notices=validation_messages(stdout+stderr)
            inserted='Insert instance layer "VK_LAYER_KHRONOS_validation"' in stderr and 'Inserted device layer "VK_LAYER_KHRONOS_validation"' in stderr
            require(result.returncode==0 and inserted and not diagnostics,f'opt{level}: native/validation failure {diagnostics[:2]}')
            native=json.loads(stdout);errors=check_record(native,token,identity,artifact,level,lambda n:(folder/n).read_bytes())
            report['runs'].append(dict(level=level,native=native,numeric=errors,validation=dict(inserted=inserted,diagnostics=diagnostics,notices=notices)))
            report['executed']=6*len(report['runs']);print(json.dumps(dict(level=level,status='pass',executed=6)),flush=True)
        require(sha256(exe.read_bytes())==exe_hash,'executable changed during run');report['status']='pass'
    except (OSError,ValueError,KeyError,TypeError,AssertionError,subprocess.SubprocessError) as error:
        report['error']=str(error);log.event('failed',error=str(error))
    finally:report['elapsed_seconds']=time.monotonic()-start;log.finish(report)
    print(json.dumps({k:report.get(k) for k in ('case_id','status','required','executed','error')}))
    return report['status']!='pass'


if __name__=='__main__':raise SystemExit(main())
