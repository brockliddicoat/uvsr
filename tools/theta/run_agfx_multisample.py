"""Check every color/depth sample, sample masks and attachment compatibility."""
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
         'crates/agfx/src/bin/multisample.rs','shaders/rust/multisample.rs']
SOURCES+=['crates/agfx/src/vulkan/'+name+'.rs' for name in ('compute','bindings','graphics','ownership','sampler','texture')]
CONFIGS=('one_float','eight_float','eight_srgb')
PHASES=('clear','partial','full','depth_reject','zero_mask')
CONTROLS=[config+'.'+name for config in CONFIGS[1:]
          for name in ('upload','readback','storage','depth_mismatch','color_mismatch','pipeline_mismatch')]


def expected(config,phase,kind):
    n=1 if config=='one_float' else 8
    active=0 if phase=='clear' else (n+1)//2 if phase=='partial' else n
    wanted=np.zeros((64,n,4),dtype='<f4')
    if kind=='depth':
        wanted[:,:,0]=.25
        for sample in range(active):wanted[:,sample,0]=.5+sample/32
    else:
        for sample in range(active):
            rgb=[sample&1,(sample>>1)&1,(sample>>2)&1] if config=='eight_srgb' else [sample/8,(7-sample)/8,.25]
            wanted[:,sample,:]=rgb+[1.0]
    return wanted


def root_hashes(config,phase):
    n=1 if config=='one_float' else 8
    if phase=='clear':return []
    if phase in ('depth_reject','zero_mask'):
        roots=[([.9,.7,.3,0],.125,255)] if phase=='depth_reject' else [([.9,.7,.3,0],.875,0)]
    else:
        middle=(n+1)//2
        roots=[(expected(config,'full','color')[0,s].tolist(),.5+s/32,1<<s)
               for s in (range(middle) if phase=='partial' else range(middle,n))]
    return [sha256(struct.pack('<8f4I',*color,depth,0,0,0,mask,0,0,0)) for color,depth,mask in roots]


def check_pixels(data,config,phase,kind):
    require(len(data)==8192,'incomplete sample readback')
    n=1 if config=='one_float' else 8
    raw=np.frombuffer(data,dtype='<f4').reshape(512,4)
    actual=raw[:64*n].reshape(64,n,4)
    wanted=expected(config,phase,kind)
    # Only the first depth component is specified by this test's interface.
    if kind=='depth':actual,wanted=actual[:,:,:1],wanted[:,:,:1]
    bad=np.argwhere(actual!=wanted)
    first=tuple(bad[0]) if len(bad) else None
    require(not len(bad),f'{config}.{phase}.{kind}: sample differs at {first}, '
            f'actual={float(actual[first]) if first else None}, expected={float(wanted[first]) if first else None}')
    if n==1:require(np.all(raw[64:]==0),'single-sample entry overwrote its untouched output suffix')


def check_record(record,token,identity,artifact,level,read):
    require(record.get('schema_version')==1 and record.get('status')=='pass' and record.get('run_token')==token,'stale or failed run')
    require(record.get('required')==record.get('executed')==30 and len(record.get('cases',[]))==30,'incomplete cases')
    require(record.get('optimization_level')==level and record.get('host_source_sha256')==identity and record.get('shader')==artifact,'stale source or shader')
    require([r.get('case_id') for r in record.get('controls',[])]==CONTROLS
            and all(r.get('status')=='pass' and r.get('diagnostic') for r in record['controls']),'missing native rejection controls')
    last=0
    for index,row in enumerate(record['cases']):
        config,phase,kind=CONFIGS[index//10],PHASES[index//2%5],('color','depth')[index%2]
        case_id=f'agfx.msaa.{config}.{phase}.{kind}.opt{level}'
        samples=1 if config=='one_float' else 8
        fmt='D32Float' if kind=='depth' else 'Rgba8Srgb' if config=='eight_srgb' else 'Rgba32Float'
        require(row.get('case_id')==case_id and row.get('status')=='pass','wrong case identity')
        require(row.get('samples')==samples and row.get('format')==fmt,'wrong sample count or format')
        require(row.get('root_sha256')==root_hashes(config,phase),'wrong shader roots')
        times=row.get('completion_values',[])
        require(len(times)==3 and all(type(t) is int for t in times) and 0<times[0]<times[1]
                and times[1]>last and times[2]==times[1]+1,'unordered completion')
        last=times[2]
        filename=case_id+'.f32';data=read(filename)
        require(row.get('output')==filename and row.get('bytes')==8192 and row.get('sha256')==sha256(data),'stale capture identity')
        check_pixels(data,config,phase,kind)
    device=record.get('device',{})
    require(device.get('validation') is True and device.get('synchronization_validation') is True
            and device.get('api_version',0)>=(1<<22|4<<12),'validation or Vulkan 1.4 missing')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('executable','sdk','shader-dir','output-dir','review'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    (output/'result.json').unlink(missing_ok=True)
    token,start=str(uuid.uuid4()),time.monotonic();log=RunLog(output,token)
    report=dict(schema_version=1,case_id='agfx.multisample.execution',status='fail',run_token=token,required=60,executed=0,runs=[],runner_sha256=sha256(Path(__file__).read_bytes()))
    try:
        exe,sdk,shaders,review_path=(p.resolve(strict=True) for p in (args.executable,args.sdk,args.shader_dir,args.review))
        identity={p:sha256((ROOT/p).read_bytes()) for p in SOURCES};exe_hash=sha256(exe.read_bytes())
        review=json.loads(review_path.read_text(encoding='utf-8'))
        require(review.get('status')=='reviewed-before-dispatch' and review.get('host_sources')==identity
                and review.get('executable_sha256')==exe_hash,'missing or stale pre-execution review')
        require(json.loads(subprocess.check_output([str(exe),'--identity'],timeout=10))==identity,'stale host rejected before Vulkan')
        report.update(executable_sha256=exe_hash,review_sha256=sha256(review_path.read_bytes()))
        env=os.environ.copy()
        for name in ('VK_LAYER_ENABLES','VK_LAYER_DISABLES','VK_LAYER_SETTINGS_PATH'):env.pop(name,None)
        env.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(sdk/'Bin'),VK_LOADER_DEBUG='layer',VK_LAYER_VALIDATE_CORE='1',VK_LAYER_VALIDATE_SYNC='1')
        env['PATH']=str(sdk/'Bin')+os.pathsep+env['PATH'];report['environment']={k:v for k,v in env.items() if k.startswith('VK_')}
        for level in (0,3):
            artifact=json.loads((shaders/f'multisample_opt{level}.metadata.json').read_text(encoding='utf-8'))
            require(artifact['payload_sha256']==sha256((shaders/f'multisample_opt{level}.spv').read_bytes())
                    ==review.get('payloads',{}).get(str(level)),'stale or unreviewed payload')
            folder=output/f'opt{level}';folder.mkdir(exist_ok=True)
            for config in CONFIGS:
                for phase in PHASES:
                    for kind in ('color','depth'):(folder/f'agfx.msaa.{config}.{phase}.{kind}.opt{level}.f32').unlink(missing_ok=True)
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
            native=json.loads(stdout);check_record(native,token,identity,artifact,level,lambda n:(folder/n).read_bytes())
            report['runs'].append(dict(level=level,native=native,validation=dict(inserted=inserted,diagnostics=diagnostics,notices=notices)))
            report['executed']=30*len(report['runs']);print(json.dumps(dict(level=level,status='pass',executed=30)),flush=True)
        require(sha256(exe.read_bytes())==exe_hash,'executable changed during run');report['status']='pass'
    except (OSError,ValueError,KeyError,TypeError,AssertionError,subprocess.SubprocessError) as error:
        report['error']=str(error);log.event('failed',error=str(error))
    finally:report['elapsed_seconds']=time.monotonic()-start;log.finish(report)
    print(json.dumps({k:report.get(k) for k in ('case_id','status','required','executed','error')}))
    return report['status']!='pass'


if __name__=='__main__':raise SystemExit(main())
