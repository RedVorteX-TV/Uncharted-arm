"""ARM hosts in the Linux launcher (launcher/uncharted_linux.py): defaults, the settings shared
with the in-game menu through bbport.ini, and the game environment.
Run: python3 -m unittest discover -s tests"""
import sys
import unittest

from paths import ROOT

if str(ROOT / 'launcher') not in sys.path:
    sys.path.insert(0, str(ROOT / 'launcher'))
import uncharted_linux as linux


class ArmLauncher(unittest.TestCase):
    def test_defaults_and_choices(self):
        ns = {'APP_DEFAULTS': {'frame_rate': '60', 'steam_deck': True},
              'FRAME_RATES': [('60', '60 FPS')], 'WINDOW_SIZES': [],
              'ANTI_ALIASING': [('off', 'Off'), ('fsr3', 'FSR 3'), ('fsr4', 'FSR 4'), ('dlss', 'DLSS')],
              'FRAME_GENERATION': [('off', 'Off'), ('fsr3', 'FSR 3'), ('dlss', 'DLSS')],
              'FRAME_GEN_MULTIPLIERS': [('2', '2'), ('3', '3'), ('4', '4')]}
        linux.apply_arm_defaults(ns)
        self.assertEqual(ns['APP_DEFAULTS']['frame_rate'], '40')
        self.assertFalse(ns['APP_DEFAULTS']['steam_deck'])
        self.assertEqual(ns['APP_DEFAULTS']['render_res'], '720')
        self.assertEqual([v for v, _t in ns['ANTI_ALIASING']], ['off', 'fsr3', 'gsr2'])
        self.assertEqual([v for v, _t in ns['FRAME_GENERATION']], ['off', 'fsr3', 'lsfg'])
        self.assertEqual([v for v, _t in ns['FRAME_GEN_MULTIPLIERS']], ['2', '3', '4'])
        linux.apply_arm_defaults(ns)  # once more: LSFG listed once
        self.assertEqual([v for v, _t in ns['FRAME_GENERATION']], ['off', 'fsr3', 'lsfg'])
        self.assertIn('40', [v for v, _t in ns['FRAME_RATES']])

    def test_ini_round_trip(self):
        values = linux.arm_ini_values({'frame_rate': '45', 'render_res': '1080', 'anti_aliasing': 'off'})
        self.assertEqual(values, {'fps_limit': '45', 'render_720p': '0', 'render_540p': '0', 'fsr1': '0',
                                  'upscaler': 'off', 'preset': '0'})
        text = ''.join(f'{k}={v}\n' for k, v in values.items())
        self.assertEqual(linux.arm_settings_from_ini('show_fps=1\n' + text),
                         {'frame_rate': '45', 'render_res': '1080', 'anti_aliasing': 'off',
                          'upscale_preset': 'off'})
        # One list: FSR 1 is upscaler=off with fsr1=1; GSR 2 with a preset.
        for method, preset in (('fsr1', 'off'), ('gsr2', 'performance'), ('fsr3', 'quality'), ('taa', 'off')):
            values = linux.arm_ini_values({'anti_aliasing': method, 'upscale_preset': preset})
            text = ''.join(f'{k}={v}\n' for k, v in values.items())
            back = linux.arm_settings_from_ini(text)
            self.assertEqual((back['anti_aliasing'], back['upscale_preset']), (method, preset))
        self.assertEqual(linux.arm_ini_values({'anti_aliasing': 'dlss'})['upscaler'], 'off')
        for res, flags in (('720', ('1', '0')), ('540', ('0', '1'))):
            values = linux.arm_ini_values({'render_res': res})
            self.assertEqual((values['render_720p'], values['render_540p']), flags)
            text = ''.join(f'{k}={v}\n' for k, v in values.items())
            self.assertEqual(linux.arm_settings_from_ini(text)['render_res'], res)
        # A launcher before 0.1.5 saved the 720p checkbox.
        self.assertEqual(linux.arm_ini_values({'render_720p': False})['render_720p'], '0')
        self.assertEqual(linux.arm_ini_values({'render_720p': True})['render_720p'], '1')
        self.assertEqual(linux.arm_ini_values({})['render_720p'], '1')
        # Both set (an edited file): 720p wins, as in run.py.
        self.assertEqual(linux.arm_settings_from_ini('render_720p=1\nrender_540p=1\n')['render_res'], '720')
        self.assertEqual(linux.arm_ini_values({'frame_rate': '144'})['fps_limit'], '40')

    def test_environment(self):
        env = {'BB_VBLANK_HZ': '40', 'BB_FSR1': '1', 'BB_UPSCALER': 'dlss', 'BB_FRAME_GEN': 'off'}
        linux.arm_environment({'anti_aliasing': 'dlss'}, env)
        # The upscaler goes to bbport.ini (arm_ini_values), not the environment.
        self.assertEqual(env, {'BB_VBLANK_HZ': '60', 'BB_FRAME_GEN': 'off'})

    def test_graphics_patches(self):
        # The effects are on by default (the games' own); one switched off applies its patch.
        env = {'BB_GRAPHICS_PATCHES': 'no_fog'}
        linux.arm_environment({'fx_ssao': False, 'fx_fog': True, 'gfx_shadows': '2'}, env)
        self.assertEqual(env['BB_GRAPHICS_PATCHES'], 'no_ssao shadows_2')
        linux.arm_environment({'gfx_shadows': '0'}, env)
        self.assertNotIn('BB_GRAPHICS_PATCHES', env)
        self.assertTrue(all(linux.ARM_GRAPHICS_DEFAULTS[k] for k, _p, _t, _h in linux.ARM_GRAPHICS_EFFECTS))
        # Every patch is in the catalog (shadows: 2 or 1 cascades).
        import json
        catalog = json.loads((ROOT / 'patches' / 'graphics_patches.json').read_text())
        ids = {p['id'] for p in catalog['patches']}
        self.assertEqual({p for _k, p, _t, _h in linux.ARM_GRAPHICS_EFFECTS} | {'shadows_2', 'shadows_1', 'movies_below_1080p'}, ids)

    def test_skin_batch_switch(self):
        env = {}
        linux.arm_environment({'arm_skin_batch': False}, env)
        self.assertEqual(env['BB_SKIN_BATCH'], '0')
        env = {}
        linux.arm_environment({}, env)
        self.assertNotIn('BB_SKIN_BATCH', env)
        self.assertTrue(linux.ARM_DEFAULTS['arm_skin_batch'])

    def test_game_rate(self):
        env = {}
        linux.arm_environment({'game_rate': '120'}, env)
        self.assertEqual(env['BB_VBLANK_HZ'], '120')
        env = {}
        linux.arm_environment({}, env)
        self.assertEqual(env['BB_VBLANK_HZ'], '60')

    def test_lsfg_dll(self):
        env = {'BB_FRAME_GEN': 'lsfg'}
        linux.arm_environment({'frame_generation': 'lsfg', 'lsfg_dll': ' /games/Lossless.dll '}, env)
        self.assertEqual(env['BB_FRAME_GEN'], 'lsfg')
        self.assertEqual(env['BB_LSFG_DLL'], '/games/Lossless.dll')
        env = {}
        linux.arm_environment({'lsfg_dll': ''}, env)
        self.assertNotIn('BB_LSFG_DLL', env)

    def test_compatibility_switches(self):
        env = {}
        linux.arm_environment({'arm_uma_readback': False, 'arm_flush_draws': True, 'arm_job_spin': False}, env)
        self.assertEqual(env.get('BB_UMA_READBACK'), '0')
        self.assertNotIn('BB_FLUSH_DRAWS', env)
        self.assertEqual(env.get('BB_JOB_SPIN'), '0')


if __name__ == '__main__':
    unittest.main()
