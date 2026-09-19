import copy
import math
from pathlib import Path
import struct
import tempfile
import unittest

from cube_oracle import CLEAR, HEIGHT, TRANSFORMS, WIDTH, check_cube_records, compare_images, inverse3, reference, trace_pixel


class CubeOracle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases = [(reference(case)[0], struct.pack(f"<{WIDTH*HEIGHT}f", *reference(case)[1])) for case in range(4)]

    def test_inverse_and_axis_aligned_box(self):
        for transform in TRANSFORMS:
            inverse = inverse3(transform)
            for row in range(3):
                for col in range(3):
                    self.assertAlmostEqual(sum(transform[row][k]*inverse[k][col] for k in range(3)), float(row == col))
        transform = ((.5,0,0,0), (0,.5,0,0), (0,0,.25,.5))
        inverse = inverse3(transform)
        point, depth, face, masked = trace_pixel(transform, inverse, 64, 64)
        self.assertEqual(point, (.015625, .015625, -1))
        self.assertEqual((depth, face, masked), (.25, (2, -1), False))
        self.assertIsNone(trace_pixel(transform, inverse, 0, 0)[0])

    def test_complete_reference(self):
        for case, (color, depth) in enumerate(self.cases):
            result = compare_images(case, color, depth)
            self.assertEqual(result['status'], 'pass')
            self.assertGreater(result['checked_pixels'], 16000)
            self.assertGreater(result['foreground_pixels'], 2000)
            self.assertGreater(result['background_pixels'], 8000)

    def test_blank_wrong_view_resource_and_sampler(self):
        for case, (color, depth) in enumerate(self.cases):
            for wrong in [bytes(CLEAR)*(WIDTH*HEIGHT), self.cases[case ^ 1][0], self.cases[case ^ 2][0]]:
                self.assertEqual(compare_images(case, wrong, depth)['status'], 'fail')

    def test_foreground_background_alpha_and_depth_corruption(self):
        for case, (color, depth) in enumerate(self.cases):
            _, _, mask, foreground = reference(case)
            points = [next(i for i in range(len(mask)) if not mask[i] and foreground[i] == wanted) for wanted in [False, True]]
            for index in points:
                for channel in range(4):
                    wrong = bytearray(color)
                    wrong[index*4+channel] ^= 1
                    self.assertEqual(compare_images(case, wrong, depth)['status'], 'fail')
                for z in [math.nan, math.inf, -1.0, 2.0, .125]:
                    wrong = bytearray(depth)
                    struct.pack_into('<f', wrong, index*4, z)
                    self.assertEqual(compare_images(case, color, wrong)['status'], 'fail')

    def test_depth_clear_and_vertical_flip(self):
        for case, (color, depth) in enumerate(self.cases):
            self.assertEqual(compare_images(case, color, struct.pack('<f', 1.0)*(WIDTH*HEIGHT))['status'], 'fail')
            flipped = b''.join(color[y*WIDTH*4:(y+1)*WIDTH*4] for y in reversed(range(HEIGHT)))
            self.assertEqual(compare_images(case, flipped, depth)['status'], 'fail')

    def test_missing_truncated_images(self):
        for color, depth in [(b'', b''), (self.cases[0][0][:-1], self.cases[0][1]), (self.cases[0][0], self.cases[0][1][:-4])]:
            with self.assertRaises(ValueError):
                compare_images(0, color, depth)


class CubeRecords(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='theta-cube-record-')
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name)
        self.metadata = {opt: {'identity': {'source_sha256': 'source'}, 'payload_sha256': f'module{opt}'} for opt in (0, 3)}
        self.rows = [{'case_id': 'theta.m2.ngapi.native_heap_cube.controls', 'status': 'pass', 'checks': 10, 'shader_cases_executed': 0}]
        for opt in (0, 3):
            for case in range(4):
                color, depth, _, _ = reference(case)
                color_file, depth_file = f'cube.opt{opt}.case{case}.rgba', f'cube.opt{opt}.case{case}.depth'
                (self.output / color_file).write_bytes(color)
                (self.output / depth_file).write_bytes(struct.pack('<16384f', *depth))
                self.rows.append(dict(case_id=f'theta.m2.ngapi.native_heap_cube.opt{opt}.case{case}', status='pass',
                    source_sha256='source', payload_sha256=f'module{opt}', view=case//2,
                    resource_index=1 if case < 2 else 3, sampler_index=2+case%2, vertex_address='0x1000',
                    nonzero_high_address_bits=False, vertex_count=24, vertex_stride=24, index_count=36,
                    root_bytes=80, width=128, height=128, image_descriptor_bytes=32, sampler_descriptor_bytes=16,
                    heap_slots=4, first_input_mismatch_byte=-1, guard_intact=True, shader_cases_executed=1,
                    color_file=color_file, depth_file=depth_file))

    def test_complete_matrix(self):
        check_cube_records(self.rows, self.metadata, self.output)
        self.assertEqual(len(list(self.output.glob('*.actual.png'))), 8)

    def test_empty_missing_duplicate(self):
        for rows in ([], self.rows[:-1], self.rows + [self.rows[-1]], self.rows[:-1] + [self.rows[0]]):
            with self.assertRaises(ValueError):
                check_cube_records(rows, self.metadata, self.output)

    def test_bad_identity_layout_ranges_and_counts(self):
        for key, value in [('status','blocked'), ('source_sha256','stale'), ('payload_sha256','stale'), ('view',1),
                           ('resource_index',3), ('sampler_index',3), ('vertex_stride',32), ('vertex_count',23),
                           ('index_count',0), ('root_bytes',72), ('width',256), ('height',0), ('heap_slots',0),
                           ('first_input_mismatch_byte',0), ('guard_intact',False), ('shader_cases_executed',0),
                           ('vertex_address','0x0'), ('vertex_address','0x1001'), ('vertex_address','0xffffffffffffff00'),
                           ('nonzero_high_address_bits',True), ('image_descriptor_bytes',0),
                           ('color_file','../outside.rgba'), ('depth_file','stale.depth')]:
            rows = copy.deepcopy(self.rows)
            rows[1][key] = value
            with self.subTest(field=key), self.assertRaises(ValueError):
                check_cube_records(rows, self.metadata, self.output)

    def test_missing_image(self):
        (self.output / self.rows[1]['color_file']).unlink()
        with self.assertRaises(OSError):
            check_cube_records(self.rows, self.metadata, self.output)

    def test_incomplete_cpu_controls(self):
        self.rows[0]['checks'] = 0
        with self.assertRaises(ValueError):
            check_cube_records(self.rows, self.metadata, self.output)


if __name__ == '__main__':
    unittest.main()
