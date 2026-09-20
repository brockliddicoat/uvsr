"""CPU controls for the source raster oracles. No Vulkan device is created."""
import struct
import unittest

import run_agfx_raster as raster


class RasterOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.images = raster.expected_outputs()

    def test_complete_source_inventory(self):
        self.assertEqual(len(raster.NAMES),50)
        self.assertEqual(len(set(raster.NAMES)),50)
        self.assertEqual(len(set(raster.GOLDENS.values())),47)
        self.assertEqual(raster.GOLDENS['indexed_offset'],'draw_indexed.png')

    def test_original_images_satisfy_independent_source_checks(self):
        for name,image in self.images.items():
            depth = None
            if name.startswith(('draw_depth_','depth_write_')):
                depth = b''.join(struct.pack('<f',raster.depth_expected(name,x,y)) for y in range(128) for x in range(128))
            self.assertEqual(raster.structural(name,image,depth)['status'],'pass',name)

    def test_alpha_corruption_is_not_hidden_by_rgb_flip(self):
        data = bytearray(self.images['draw_triangle']); data[3] = 0
        self.assertEqual(raster.structural('draw_triangle',data,None)['status'],'fail')

    def test_blank_points_fail_exact_source_coverage(self):
        data = bytes([0,0,0,255])*16384
        self.assertEqual(raster.structural('draw_points',data,None)['status'],'fail')

    def test_blank_lines_fail_source_coverage_floor(self):
        data = bytes([0,0,0,255])*16384
        for name in ['draw_lines','draw_lines_indexed','draw_wireframe']:
            self.assertEqual(raster.structural(name,data,None)['status'],'fail')

    def test_viewport_and_scissor_leaks_fail(self):
        for name in ['viewport','scissor_rect']:
            data = bytearray(self.images[name]); data[0] = 255
            self.assertEqual(raster.structural(name,data,None)['status'],'fail')

    def test_discard_must_kill_covered_fragments(self):
        data = bytearray(self.images['draw_fragment_discard']); data[(80*128+100)*4] = 255
        self.assertEqual(raster.structural('draw_fragment_discard',data,None)['status'],'fail')

    def test_load_clear_and_dont_care_have_distinct_contracts(self):
        for name in ['render_pass_action_load','render_pass_action_clear','render_pass_action_dont_care']:
            data = bytearray(self.images[name]); data[0:4] = bytes([255,0,255,255])
            self.assertEqual(raster.structural(name,data,None)['status'],'fail')

    def test_depth_nan_or_wrong_value_fails_full_readback(self):
        name = 'draw_depth_test_less'
        good = struct.pack('<f',0.5)*16384
        for value in [float('nan'),0.75]:
            depth = struct.pack('<f',value)+good[4:]
            self.assertEqual(raster.structural(name,self.images[name],depth)['status'],'fail')

    def test_blend_alpha_has_an_independent_arithmetic_oracle(self):
        name = 'draw_alpha_blend'; data = bytearray(self.images[name]); data[3] = 255
        self.assertEqual(raster.structural(name,data,None)['status'],'fail')


if __name__ == '__main__': unittest.main()
