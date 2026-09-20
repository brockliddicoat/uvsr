"""Reject incomplete, stale, reordered and independently incorrect Zoom2D evidence."""
import unittest
from unittest.mock import patch
import numpy as np
from run_s2h_zoom import check_record, expected_states, inputs, root_bytes, sha256, structural


class ZoomEvidence(unittest.TestCase):
    def setUp(self):
        self.files = {}
        self.record = dict(schema_version=1, status='pass', run_token='fresh', required=29, executed=29,
                           optimization_level=0, host_source_sha256={'host':'current'}, shader={'shader':'current'},
                           cases=[], device=dict(validation=True, synchronization_validation=True,
                                                 api_version=1<<22|4<<12, loader_api_version=1<<22|4<<12))
        previous, before = [0,0,0,0], bytes(112)
        image = bytes([25,25,89,255])*480000
        for index, (name,*source_mouse) in enumerate(inputs()):
            mouse = np.asarray(source_mouse,dtype=np.float32).astype(float).tolist()
            pre, post = expected_states(index)
            case_id = f's2h.zoom.{name}.opt0'
            row = dict(case_id=case_id, status='pass', mouse=mouse, previous_mouse=previous,
                       root_sha256=sha256(root_bytes(mouse,previous)), before_state_sha256=sha256(before),
                       completion_values=list(range(3+index*6,9+index*6)))
            for key,ext,data in [('pre','pre',pre),('post','post',post),('image','rgba8',image)]:
                filename=case_id+'.'+ext
                self.files[filename]=data
                row[key]=dict(output=filename,bytes=len(data),sha256=sha256(data))
            self.record['cases'].append(row)
            previous,before=mouse,post

    def check(self):
        with patch('run_s2h_zoom.structural',return_value=123):
            return check_record(self.record,'fresh',{'host':'current'},{'shader':'current'},0,self.files.__getitem__)

    def test_zoom_001_valid_fractional_input_record(self):
        self.assertEqual(self.check(),123)

    def test_zoom_002_zero_case_run_rejected(self):
        self.record.update(required=0,executed=0,cases=[])
        with self.assertRaisesRegex(ValueError,'incomplete'): self.check()

    def test_zoom_003_stale_host_rejected(self):
        self.record['host_source_sha256']={'host':'old'}
        with self.assertRaisesRegex(ValueError,'source identity'): self.check()

    def test_zoom_004_wrong_root_rejected(self):
        self.record['cases'][17]['root_sha256']='old'
        with self.assertRaisesRegex(ValueError,'input root'): self.check()

    def test_zoom_005_reordered_phases_rejected(self):
        self.record['cases'][0]['completion_values']=[5,6,3,4,7,8]
        with self.assertRaisesRegex(ValueError,'order'): self.check()

    def test_zoom_006_corrupt_state_with_matching_hash_rejected(self):
        row=self.record['cases'][0]['pre']
        data=bytearray(self.files[row['output']]);data[92]=1
        self.files[row['output']]=bytes(data);row['sha256']=sha256(data)
        with self.assertRaisesRegex(ValueError,'incorrect pre state'): self.check()

    def test_zoom_007_broken_state_history_rejected(self):
        self.record['cases'][1]['before_state_sha256']='old'
        with self.assertRaisesRegex(ValueError,'state chain'): self.check()

    def test_zoom_008_incomplete_image_rejected(self):
        row=self.record['cases'][0]['image']
        self.files[row['output']]=self.files[row['output']][:-4]
        with self.assertRaisesRegex(ValueError,'incomplete or stale image'): self.check()

    def test_zoom_009_transparent_image_rejected(self):
        with self.assertRaisesRegex(ValueError,'nonopaque'):
            structural({'idle':np.zeros((600,800,4),dtype=np.uint8)})


if __name__ == '__main__':
    unittest.main()
