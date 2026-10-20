import importlib.util
import struct
import unittest

from paths import ROOT

spec = importlib.util.spec_from_file_location('render_res', ROOT / 'scripts' / 'render_res.py')
render_res = importlib.util.module_from_spec(spec)
spec.loader.exec_module(render_res)

MIB = 1 << 20


def image(base=0x47):
    """A stand-in for boot-linked.bin: the display settings stores, the memory table and its check."""
    data = bytearray(b'\x90' * 0x200)
    for disp, value in [(0x08, 1920), (0x0c, 1080), (0x18, 1920), (0x1c, 1080), (0x24, 1920), (0x28, 1080),
                        (0x30, 2560), (0x34, 1216), (0x38, 1920), (0x3c, 1080), (0x40, 240), (0x44, 270)]:
        data += bytes([0xc7, base, disp]) + value.to_bytes(4, 'little')
    data += bytes.fromhex('48b8') + (1920).to_bytes(4, 'little') + (1080).to_bytes(4, 'little')
    data += b'\x90' * 0x900
    for entry_id, parent, size, align in [(2, 2, 1536 * MIB, 0x100000), (3, 2, 2988 * MIB, 0x100000),
                                          (0x85, 6, 497 * MIB, 0x80), (0x84, 6, 128 * MIB, 0x80)]:
        data += struct.pack('<IIQQQ', entry_id, parent, size, align, 0)
    data += bytes.fromhex('488d75e048ba') + (0x120000000).to_bytes(8, 'little') + bytes(64)
    return data


class UpscalePresets(unittest.TestCase):
    def run_sizes(self, exe, env):
        import shutil
        import sys
        import tempfile
        from pathlib import Path
        sys.path.insert(0, str(ROOT))
        import run
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            (out / 'boot-linked.bin').write_bytes(image())
            env = dict(env)
            run.render_resolution(env, exe, out, out / 'boot-linked.bin')
            shutil.rmtree(out, ignore_errors=True)
            return env

    def test_quality_1440p(self):
        env = self.run_sizes('big2-ps4_Shipping.elf', {'BB_RESOLUTION': '2560x1440', 'BB_UPSCALE_PRESET': 'quality'})
        self.assertEqual(env['BB_RENDER_RES'], '1712x960')
        self.assertEqual(env['BB_OUTPUT_RES'], '2560x1440')
        self.assertEqual(env['BB_SCENE_SIZE'], '1712x960')
        self.assertEqual(env['BB_UI_COMPOSITE_PS'], '749bbec9')

    def test_native_1080p_untouched(self):
        env = self.run_sizes('big3-ps4_Shipping.elf', {'BB_RESOLUTION': '1920x1080'})
        self.assertNotIn('BB_RENDER_RES', env)
        self.assertNotIn('BB_MOVIE_SIZE', env)

    def test_drakes_fortune_4k_upscaled_from_1620p(self):
        env = self.run_sizes('eboot.bin', {'BB_RESOLUTION': '3840x2160'})
        self.assertEqual(env['BB_RENDER_RES'], '2880x1620')
        self.assertEqual(env['BB_OUTPUT_RES'], '3840x2160')


class RenderResolution(unittest.TestCase):
    def test_1440p_stores_and_memory(self):
        patched, render, extra = render_res.patch_image(image(), 'u2', 2560, 1440)
        self.assertEqual(render, (2560, 1440))
        self.assertGreater(extra, 0)
        for disp, value in [(0x08, 2560), (0x0c, 1440), (0x40, 320), (0x44, 360)]:
            self.assertIn(bytes([0xc7, 0x47, disp]) + value.to_bytes(4, 'little'), patched)
        self.assertNotIn(bytes([0xc7, 0x47, 0x08]) + (1920).to_bytes(4, 'little'), patched)

    def test_rax_form(self):  # 1.02 stores through rax
        patched, render, _extra = render_res.patch_image(image(0x40), 'u3', 2560, 1440)
        self.assertIn(bytes([0xc7, 0x40, 0x08]) + (2560).to_bytes(4, 'little'), patched)

    def test_other_base_register(self):  # CUSA02344 builds the settings through other registers
        patched, _render, _extra = render_res.patch_image(image(0x46), 'u2', 2560, 1440)
        self.assertIn(bytes([0xc7, 0x46, 0x08]) + (2560).to_bytes(4, 'little'), patched)
        self.assertIn(bytes([0xc7, 0x46, 0x44]) + (360).to_bytes(4, 'little'), patched)

    def test_drakes_fortune_steps_down(self):
        _patched, render, _extra = render_res.patch_image(image(), 'u1', 3840, 2160)
        self.assertEqual(render, (2880, 1620))

    def test_4k_moves_heap_for_u2_u3(self):
        _patched, render, extra = render_res.patch_image(image(), 'u3', 3840, 2160)
        self.assertEqual(render, (3840, 2160))
        self.assertGreater(extra, 2000)

    def test_bad_size(self):
        with self.assertRaises(SystemExit):
            render_res.parse_size('2561x1440')


if __name__ == '__main__':
    unittest.main()
