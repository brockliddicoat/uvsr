"""Controls for per-sample evidence, without native execution."""
import copy
import struct
import unittest
import numpy as np
from run_agfx_multisample import CONFIGS, PHASES, CONTROLS, check_record, expected, root_hashes, sha256


class MultisampleEvidence(unittest.TestCase):
    def setUp(self):
        self.files={}
        self.record=dict(schema_version=1,status='pass',run_token='fresh',required=30,executed=30,
            optimization_level=0,host_source_sha256={'source':'current'},shader={'payload':'current'},
            device=dict(validation=True,synchronization_validation=True,api_version=1<<22|4<<12),
            controls=[dict(case_id=n,status='pass',diagnostic='rejected') for n in CONTROLS],cases=[])
        for config in CONFIGS:
            for phase in PHASES:
                for kind in ('color','depth'):
                    n=1 if config=='one_float' else 8
                    values=np.zeros((512,4),dtype='<f4')
                    values[:64*n]=expected(config,phase,kind).reshape(64*n,4)
                    data=values.tobytes();case_id=f'agfx.msaa.{config}.{phase}.{kind}.opt0'
                    filename=case_id+'.f32';self.files[filename]=data
                    start=2+3*len(self.record['cases'])
                    self.record['cases'].append(dict(case_id=case_id,status='pass',samples=n,
                        format='D32Float' if kind=='depth' else 'Rgba8Srgb' if config=='eight_srgb' else 'Rgba32Float',
                        output=filename,bytes=len(data),sha256=sha256(data),root_sha256=root_hashes(config,phase),
                        completion_values=[start,start+1,start+2]))

    def check(self):check_record(self.record,'fresh',{'source':'current'},{'payload':'current'},0,self.files.__getitem__)

    def alter(self,index,data):
        row=self.record['cases'][index];self.files[row['output']]=data
        row.update(bytes=len(data),sha256=sha256(data))

    def test_msaa_001_complete_case_matrix(self):self.check()

    def test_msaa_002_missing_duplicate_and_zero_cases(self):
        original=copy.deepcopy(self.record)
        for key,value in [('executed',0),('required',0),('cases',[]),('cases',[original['cases'][0]]*30)]:
            self.record=copy.deepcopy(original);self.record[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check()

    def test_msaa_003_one_wrong_sample_in_each_channel_is_detected(self):
        row=self.record['cases'][14];original=self.files[row['output']]
        for channel in range(4):
            data=bytearray(original);offset=(19*8*4+6*4+channel)*4
            data[offset:offset+4]=struct.pack('<f',.123)
            self.alter(14,bytes(data))
            with self.subTest(channel=channel),self.assertRaisesRegex(ValueError,'sample differs'):self.check()

    def test_msaa_004_unwritten_sample_and_depth_rejection(self):
        for index,offset in [(12,7*16),(13,7*16),(17,3*16),(19,7*16)]:
            original=self.files[self.record['cases'][index]['output']]
            data=bytearray(original);data[offset:offset+4]=struct.pack('<f',.875)
            self.alter(index,bytes(data))
            with self.subTest(index=index),self.assertRaisesRegex(ValueError,'sample differs'):self.check()
            self.alter(index,original)

    def test_msaa_005_tail_and_incomplete_capture(self):
        original=self.files[self.record['cases'][0]['output']]
        data=bytearray(original);data[64*16:64*16+4]=struct.pack('<f',1)
        self.alter(0,bytes(data))
        with self.assertRaisesRegex(ValueError,'suffix'):self.check()
        self.alter(0,original[:-4])
        with self.assertRaisesRegex(ValueError,'capture identity'):self.check()

    def test_msaa_006_stale_identity_validation_or_rejection_evidence(self):
        original=copy.deepcopy(self.record)
        for key,value in [('run_token','old'),('host_source_sha256',{}),('shader',{}),('optimization_level',3),('device',{}),('controls',[])]:
            self.record=copy.deepcopy(original);self.record[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check()

    def test_msaa_007_roots_sample_count_and_completion(self):
        original=copy.deepcopy(self.record)
        for key,value in [('samples',1),('format','Rgba8Unorm'),('root_sha256',[]),('completion_values',[5,4,6])]:
            self.record=copy.deepcopy(original);self.record['cases'][14][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):self.check()

    def test_msaa_008_nan_depth_and_reordered_samples(self):
        row=self.record['cases'][15];original=self.files[row['output']]
        for bad in (struct.pack('<f',float('nan'))+original[4:], original[16:32]+original[:16]+original[32:]):
            self.alter(15,bad)
            with self.assertRaisesRegex(ValueError,'sample differs'):self.check()


if __name__=='__main__':unittest.main()
