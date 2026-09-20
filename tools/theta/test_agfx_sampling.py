"""Sampling evidence controls. No native execution or GPU is used."""
import copy
import unittest
from run_agfx_sampling import CONTROLS, NAMES, acceptance, check_record, expected_outputs, root, sha256, structural_bounds


class SamplingRecords(unittest.TestCase):
    def setUp(self):
        self.expected = expected_outputs()
        self.outputs = {f'agfx.sampling.{name}.opt{level}': self.expected[name]
                        for level in (0,3) for name in NAMES}
        self.record = dict(schema_version=1, run_token='fresh', status='executed', required=16, executed=16,
            host_source_sha256={'source':'hash'}, shaders=['opt0','opt3'], comparison_creations=8,
            device=dict(validation=True, synchronization_validation=True,
                        api_version=1<<22|4<<12, loader_api_version=1<<22|4<<12),
            controls=[dict(case_id=name,status='pass') for name in CONTROLS], cases=[])
        for index, (name,data) in enumerate(self.outputs.items()):
            short = NAMES[index%8]
            completed = index//8*20+4+index%8*2+(short=='sample_2d')
            self.record['cases'].append(dict(case_id=name,status='executed',bytes=len(data),sha256=sha256(data),
                root_bytes=len(root(short)),root_sha256=sha256(root(short)),completion_values=[completed,completed+1]))

    def check(self, record=None, outputs=None):
        return check_record(self.record if record is None else record, 'fresh', {'source':'hash'},
            ['opt0','opt3'], self.outputs if outputs is None else outputs, self.expected)

    def test_source_goldens_seed_and_border_exact(self):
        self.assertEqual([row['status'] for row in self.check()], ['pass']*16)
        self.assertEqual(self.expected['seed'][:4], bytes([0,0,0,255]))
        self.assertEqual(self.expected['seed'][-4:], bytes([255,255,0,255]))
        self.assertEqual(self.expected['address_border'][:4], bytes(4))
        # Uploaded integer truncation and hardware UNORM seed remain distinct.
        self.assertNotEqual(self.expected['sample_2d'], self.expected['filter_linear'])

    def test_missing_duplicate_zero_and_skipped_cases(self):
        for key, value in [('cases',[]),('cases',[self.record['cases'][0]]*16),('required',0),
                           ('executed',None),('status','skip'),('run_token','old')]:
            record=copy.deepcopy(self.record)
            record[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)

    def test_pixel_failure_even_with_candidate_hash_updated(self):
        for name, data in self.outputs.items():
            outputs=self.outputs.copy()
            changed=bytearray(data)
            changed[3] ^= 1
            outputs[name]=bytes(changed)
            record=copy.deepcopy(self.record)
            for row in record['cases']:
                if row['case_id']==name: row['sha256']=sha256(outputs[name])
            result=self.check(record,outputs)
            failures=[row for row in result if row['status']=='fail']
            self.assertEqual(len(failures),1)
            self.assertEqual(failures[0]['first_difference']['channel'],3)

    def test_root_completion_and_payload_identity(self):
        for key,value in [('root_bytes',16),('root_sha256','changed'),('completion_values',[0,1]),('sha256','stale')]:
            record=copy.deepcopy(self.record)
            record['cases'][1][key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)
        for key,value in [('host_source_sha256',{}),('shaders',[])]:
            record=copy.deepcopy(self.record)
            record[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)

    def test_native_controls_and_validation_required(self):
        for key,value in [('controls',[]),('comparison_creations',0),('device',{})]:
            record=copy.deepcopy(self.record)
            record[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)
        record=copy.deepcopy(self.record)
        record['controls'][0]['status']='fail'
        with self.assertRaises(ValueError): self.check(record)

    def test_short_output_cannot_be_a_partial_pass(self):
        outputs=self.outputs.copy()
        name=next(iter(outputs))
        outputs[name]=outputs[name][:-1]
        record=copy.deepcopy(self.record)
        record['cases'][0].update(sha256=sha256(outputs[name]),bytes=len(outputs[name]))
        with self.assertRaises(ValueError): self.check(record,outputs)

    def accept(self, outputs=None, scores=None):
        outputs = self.outputs if outputs is None else outputs
        comparisons = self.check()
        flips = {row['case_id']:dict(mean=0.0,maximum=0.0,threshold=.05,
                    reference_sha256=sha256(self.expected[NAMES[i%8]]),actual_sha256=sha256(outputs[row['case_id']]))
                 for i,row in enumerate(comparisons) if NAMES[i%8] not in ('seed','address_border')}
        if scores is not None: scores(flips)
        return acceptance(comparisons,outputs,self.expected,flips)

    def test_source_acceptance_has_independent_full_image_checks(self):
        self.assertEqual([row['status'] for row in self.accept()], ['pass']*16)
        outputs=self.outputs.copy()
        outputs['agfx.sampling.filter_linear.opt0']=outputs['agfx.sampling.filter_nearest.opt0']
        self.assertEqual(self.accept(outputs)[2]['structural_status'],'fail')
        outputs=self.outputs.copy()
        outputs['agfx.sampling.address_repeat.opt0']=outputs['agfx.sampling.address_clamp_to_edge.opt0']
        self.assertEqual(self.accept(outputs)[3]['status'],'fail')

    def test_unorm_adjacent_integers_allowed_but_wrong_pattern_rejected(self):
        lo,hi=structural_bounds('seed',bytes())
        for seed in (lo,hi):
            outputs=self.outputs.copy()
            outputs['agfx.sampling.seed.opt0']=bytes(seed)
            self.assertEqual(self.accept(outputs)[0]['structural_status'],'pass')
        outputs=self.outputs.copy()
        broken=bytearray(lo);broken[32*4]-=1
        outputs['agfx.sampling.seed.opt0']=bytes(broken)
        self.assertEqual(self.accept(outputs)[0]['status'],'fail')

    def test_original_flip_threshold_preserved_and_required(self):
        name='agfx.sampling.filter_linear.opt0'
        def at_limit(scores): scores[name].update(mean=.0500000007,maximum=.0500000007)
        def too_high(scores): scores[name].update(mean=.050001,maximum=.1)
        self.assertEqual(self.accept(scores=at_limit)[2]['status'],'pass')
        self.assertEqual(self.accept(scores=too_high)[2]['status'],'fail')
        with self.assertRaises(ValueError): self.accept(scores=lambda rows:rows.pop(name))
        with self.assertRaises(ValueError): self.accept(scores=lambda rows:rows[name].update(threshold=.051))

    def test_flip_cannot_hide_alpha_or_change_expected_identity(self):
        outputs=self.outputs.copy()
        name='agfx.sampling.filter_linear.opt0'
        data=bytearray(outputs[name]);data[3]=254;outputs[name]=bytes(data)
        row=self.accept(outputs)[2]
        self.assertEqual(row['alpha_status'],'fail')
        self.assertEqual(row['status'],'fail')
        with self.assertRaises(ValueError): self.accept(scores=lambda rows:rows[name].update(reference_sha256='stale'))


if __name__=='__main__': unittest.main()
