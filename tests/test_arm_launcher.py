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
        self.assertTrue(ns['APP_DEFAULTS']['render_720p'])
        self.assertEqual([v for v, _t in ns['ANTI_ALIASING']], ['off', 'fsr3'])
        self.assertEqual([v for v, _t in ns['FRAME_GENERATION']], ['off', 'fsr3', 'lsfg'])
        self.assertEqual([v for v, _t in ns['FRAME_GEN_MULTIPLIERS']], ['2', '3', '4'])
        linux.apply_arm_defaults(ns)  # once more: LSFG listed once
        self.assertEqual([v for v, _t in ns['FRAME_GENERATION']], ['off', 'fsr3', 'lsfg'])
        self.assertIn('40', [v for v, _t in ns['FRAME_RATES']])

    def test_ini_round_trip(self):
        values = linux.arm_ini_values({'frame_rate': '45', 'render_720p': False, 'scaler': 'bilinear'})
        self.assertEqual(values, {'fps_limit': '45', 'render_720p': '0', 'fsr1': '0'})
        text = ''.join(f'{k}={v}\n' for k, v in values.items())
        self.assertEqual(linux.arm_settings_from_ini('show_fps=1\n' + text),
                         {'frame_rate': '45', 'render_720p': False, 'scaler': 'bilinear'})
        self.assertEqual(linux.arm_ini_values({'frame_rate': '144'})['fps_limit'], '40')

    def test_environment(self):
        env = {'BB_VBLANK_HZ': '40', 'BB_FSR1': '1', 'BB_UPSCALER': 'dlss', 'BB_FRAME_GEN': 'off'}
        linux.arm_environment({'anti_aliasing': 'dlss'}, env)
        self.assertEqual(env, {'BB_VBLANK_HZ': '60', 'BB_UPSCALER': 'off', 'BB_FRAME_GEN': 'off'})

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
