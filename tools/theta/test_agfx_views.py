"""CPU controls for format-view evidence. No native execution."""
import copy
import struct
import unittest
import numpy as np
from run_agfx_views import CONFIGS, CONTROLS, check_record, sha256


class ViewEvidence(unittest.TestCase):
    def setUp(self):
        self.files={}
        self.record=dict(schema_version=1,status='pass',run_token='fresh',required=6,executed=6,
            optimization_level=0,host_source_sha256={'source':'current'},shader={'payload':'current'},
            device=dict(validation=True,synchronization_validation=True,api_version=1<<22|4<<12),
            controls=[dict(case_id=n,status='pass',diagnostic='rejected') for n in CONTROLS],cases=[])
        for i in range(6):
            name,phase=CONFIGS[i//2],('storage','raster')[i%2]
            code=[124,170,203,204] if i in (3,5) else [51,102,153,204]
            linear=np.array(code,dtype=np.float64)/255
            sampled=linear.copy()
            if i>=2: sampled[:3]=((linear[:3]+.055)/1.055)**2.4
            case_id=f'agfx.views.{name}.{phase}.opt0'
            start=(3,9,16,22,29,35)[i]
            row=dict(case_id=case_id,status='pass',base_format='Rgba8Srgb' if i>=4 else 'Rgba8Unorm',
                view_format='Rgba8Srgb' if i>=2 else 'Rgba8Unorm',
                root_sha256=sha256(struct.pack('<4f',.2,.4,.6,.8)),completion_values=list(range(start,start+6)))
            for key,ext,data in [('encoded','rgba8',bytes(code)*64),
                                 ('sampled','sampled.f32',np.tile(sampled.astype('<f4'),64).tobytes()),
                                 ('storage','storage.f32',np.tile(linear.astype('<f4'),64).tobytes())]:
                filename=case_id+'.'+ext;self.files[filename]=data
                row[key]=dict(output=filename,bytes=len(data),sha256=sha256(data))
            self.record['cases'].append(row)

    def check(self):
        check_record(self.record,'fresh',{'source':'current'},{'payload':'current'},0,self.files.__getitem__)

    def alter(self,index,key,data):
        row=self.record['cases'][index][key]
        self.files[row['output']]=data;row.update(bytes=len(data),sha256=sha256(data))

    def test_views_001_valid_conversions(self): self.check()

    def test_views_002_missing_or_duplicate_cases(self):
        initial=copy.deepcopy(self.record)
        for key,value in [('required',0),('executed',None),('cases',[]),('cases',[initial['cases'][0]]*6)]:
            self.record=copy.deepcopy(initial);self.record[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): self.check()

    def test_views_003_stale_identity_or_completion(self):
        initial=copy.deepcopy(self.record)
        for key,value in [('run_token','old'),('host_source_sha256',{}),('shader',{}),('optimization_level',3)]:
            self.record=copy.deepcopy(initial);self.record[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): self.check()
        self.record=initial;self.record['cases'][0]['completion_values']=[3,4,5,7,6,8]
        with self.assertRaises(ValueError): self.check()

    def test_views_004_wrong_encoded_bytes_despite_matching_hash(self):
        self.alter(3,'encoded',bytes([51,102,153,204])*64)
        with self.assertRaisesRegex(ValueError,'encoded'): self.check()

    def test_views_005_missing_srgb_decode(self):
        self.alter(2,'sampled',np.tile(np.array([.2,.4,.6,.8],dtype='<f4'),64).tobytes())
        with self.assertRaisesRegex(ValueError,'sampled: view conversion'): self.check()

    def test_views_006_storage_must_not_decode(self):
        self.alter(2,'storage',self.files[self.record['cases'][2]['sampled']['output']])
        with self.assertRaisesRegex(ValueError,'storage: view conversion'): self.check()

    def test_views_007_alpha_and_nonfinite_values(self):
        original=self.files[self.record['cases'][2]['sampled']['output']]
        for value in (.6,float('nan'),float('inf')):
            data=bytearray(original);data[12:16]=struct.pack('<f',value)
            self.alter(2,'sampled',bytes(data))
            with self.subTest(value=value),self.assertRaises(ValueError): self.check()

    def test_views_008_incomplete_readback(self):
        self.alter(0,'storage',bytes(1020))
        with self.assertRaisesRegex(ValueError,'incomplete or stale'): self.check()

    def test_views_009_format_and_validation_controls(self):
        original=copy.deepcopy(self.record)
        for key,value in [('device',{}),('controls',[])]:
            self.record=copy.deepcopy(original);self.record[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): self.check()
        self.record=original;self.record['cases'][2]['view_format']='Rgba8Unorm'
        with self.assertRaisesRegex(ValueError,'format identity'): self.check()

    def test_views_010_roundtrip_rejects_adjacent_code(self):
        # A changed code cannot hide inside a global floating-point tolerance.
        rgb=np.array([52,102,153],dtype=np.float64)/255
        wrong=np.r_[((rgb+.055)/1.055)**2.4,.8].astype('<f4')
        self.alter(2,'sampled',np.tile(wrong,64).tobytes())
        with self.assertRaisesRegex(ValueError,'roundtrip'): self.check()


if __name__=='__main__': unittest.main()
