"""Evidence failure controls for documentation images and persistent UI."""
import copy
import struct
import unittest

from run_s2h_demos import COUNTS, DOCS, FEATURES, STEPS, check_record, root_bytes, sha256, state_words


class DocumentationRecords(unittest.TestCase):
    def setUp(self):
        self.files = {}
        image = bytes([102, 178, 102, 255]) * 480000
        self.record = dict(schema_version=1, status='pass', run_token='fresh', optimization_level=0,
            host_source_sha256={'source': 'hash'}, shader={'payload': 'hash'}, required=37, executed=37,
            cases=[], interactions=[], device=dict(validation=True, synchronization_validation=True,
            api_version=4210688, loader_api_version=4210688, enabled_features=sorted(FEATURES)))

        def capture(stem, actual=image):
            path = stem + '-opt0.rgba8'; self.files[path] = actual
            return dict(output=path, bytes=len(actual), sha256=sha256(actual))

        last = 2
        for name, branch in DOCS:
            category = list(COUNTS).index(name); stem = f'docs-{name}-{branch}'
            actual = image
            if (name, branch) == ('2d', 4):
                offset = (20 * 800 + 110) * 4
                actual = image[:offset] + bytes([255, 0, 0, 255]) + image[offset + 4:]
            self.record['cases'].append(dict(case_id=f's2h.{stem}.opt0', status='pass', category=category,
                branch=branch, image=capture(stem, actual), source_agreement='not checked',
                root_sha256=sha256(root_bytes(category, branch)), completion_values=[last + 2, last + 3]))
            last += 3
        last += 1
        before, previous = bytes(80), [0, 0, 0, 0]
        for step in STEPS:
            name, branch, x, y, pressed = step[:5]; mouse = [x, y, pressed, 0]
            words = state_words(step); actual = struct.pack('<20I', *words)
            path = f'ui-{name}-opt0.state'; self.files[path] = actual
            self.record['interactions'].append(dict(case_id=f's2h.docs-ui.{name}.opt0', status='pass',
                branch=branch, mouse=mouse, previous_mouse=previous,
                root_sha256=sha256(root_bytes(4, branch, mouse, previous)), before_state_sha256=sha256(before),
                after_state_words=words, state=dict(output=path, bytes=80, sha256=sha256(actual), expected_sha256=sha256(actual)),
                image=capture('ui-' + name), completion_values=list(range(last + 1, last + 5))))
            last += 4; before, previous = actual, mouse

    def check(self, record=None):
        check_record(self.record if record is None else record, 'fresh', {'source': 'hash'}, {'payload': 'hash'}, 0,
                     self.files.__getitem__)

    def test_all_required_images_and_state_transitions(self):
        self.check()

    def test_missing_zero_or_duplicate_cases(self):
        for key, value in [('required', 0), ('executed', 0), ('cases', []), ('interactions', []),
                           ('cases', self.record['cases'][:-1] + self.record['cases'][:1])]:
            record = copy.deepcopy(self.record); record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)

    def test_state_cannot_validate_itself_with_matching_hashes(self):
        record = copy.deepcopy(self.record); row = record['interactions'][5]
        wrong = bytes(80)
        self.files[row['state']['output']] = wrong
        row['after_state_words'] = [0] * 20
        row['state']['sha256'] = row['state']['expected_sha256'] = sha256(wrong)
        with self.assertRaises(ValueError): self.check(record)

    def test_frame_order_input_and_completion_are_required(self):
        for key, value in [('previous_mouse', [0, 0, 0, 0]), ('completion_values', [1, 2, 3, 4]),
                           ('before_state_sha256', sha256(bytes(80)))]:
            record = copy.deepcopy(self.record); record['interactions'][6][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)

    def test_image_alpha_and_length_are_checked_beyond_hash(self):
        for actual in [bytes([102, 178, 102, 0]) * 480000, bytes(12)]:
            record = copy.deepcopy(self.record); row = record['cases'][0]
            self.files[row['image']['output']] = actual
            row['image']['sha256'] = sha256(actual); row['image']['bytes'] = len(actual)
            with self.subTest(size=len(actual)), self.assertRaises(ValueError): self.check(record)

    def test_identity_validation_and_source_parity_are_distinct(self):
        for key, value in [('run_token', 'old'), ('host_source_sha256', {}), ('shader', {}), ('status', 'skip')]:
            record = copy.deepcopy(self.record); record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): self.check(record)
        record = copy.deepcopy(self.record); record['device']['validation'] = False
        with self.assertRaises(ValueError): self.check(record)
        record = copy.deepcopy(self.record); record['cases'][0]['source_agreement'] = 'pass'
        with self.assertRaises(ValueError): self.check(record)


if __name__ == '__main__':
    unittest.main()
