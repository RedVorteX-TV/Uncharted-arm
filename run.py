#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepares an Uncharted executable of the Nathan Drake Collection and starts bb-probe.exe.

BB_GAME_DIR   the game folder (CUSA02343, CUSA02320, ...; the one with eboot.bin)
BB_GAME_DIR_U1 / _U2 / _U3   optional: the folder of a game that was dumped on its own; used for
              that game instead of BB_GAME_DIR (its executable may be that folder's eboot.bin)
BB_EXE        u1 / u2 / u3 or an executable name (default u1: eboot.bin, the collection menu)
BB_USER_DIR   saves and shader caches (default user/ beside this script)
BB_RESOLUTION / BB_RESOLUTION_U1 / _U2 / _U3   resolution WxH (default 1920x1080, the PS4's):
              the game's display settings and memory table are patched for the size it draws at
              (scripts/render_res.py, after BilalReloaded's Uncharted PC port)
BB_UPSCALE_QUALITY / _U1 / _U2 / _U3   quality, balanced, performance or ultra: the game draws
              at the resolution divided by 1.5 / 1.7 / 2 / 3 and DLSS / FSR upscales to it
Extra arguments go to bb-probe. When the game starts another executable of the collection
(sceSystemServiceLoadExec), that one is prepared and started in its place.
The in-game "Apply and restart" runs this script again through BB_RESTART_COMMAND;
`--after PID` waits for the previous game process to end first (its GPU device and memory).
"""
import ctypes
import os
import platform
from pathlib import Path
import shlex
import subprocess
import sys

PORT = Path(__file__).resolve().parent
EXECUTABLES = {'u1': 'eboot.bin', 'u2': 'big2-ps4_Shipping.elf', 'u3': 'big3-ps4_Shipping.elf'}
LOADEXEC_EXIT = 101  # runtime_services.c: the game asked for another executable
WINDOWS = sys.platform == 'win32'
PROBE = 'bb-probe.exe' if WINDOWS else 'bb-probe'


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def wait_for(pid):
    if not WINDOWS:
        # Linux: an in-game restart execs this script in the game's own process.
        if pid == os.getpid():
            return
        import time
        for _ in range(300):
            try:
                os.kill(pid, 0)
            except OSError:
                return
            time.sleep(0.1)
        return
    kernel32 = ctypes.windll.kernel32
    SYNCHRONIZE = 0x00100000
    handle = kernel32.OpenProcess(SYNCHRONIZE, False, pid)
    if handle:
        kernel32.WaitForSingleObject(handle, 30000)
        kernel32.CloseHandle(handle)


def no_console():
    """Started without a console (the launcher): console programs must not open their own."""
    if not WINDOWS:
        return 0
    return 0 if ctypes.windll.kernel32.GetConsoleWindow() else subprocess.CREATE_NO_WINDOW


def run_script(name, *args):
    # Inside the packaged executable (PyInstaller) the executable runs scripts itself.
    script = ['--script'] if getattr(sys, 'frozen', False) else []
    command = [sys.executable, *script, str(PORT / 'scripts' / name), *map(str, args)]
    # stdin given: the output handles are passed explicitly (a windowed child would get none).
    result = subprocess.run(command, stdin=subprocess.DEVNULL, creationflags=no_console())
    if result.returncode:
        sys.exit(result.returncode)


def find_executable(name):
    """bin/ (packaged), else out/ (built with build.sh)."""
    for candidate in (PORT / 'bin' / name, PORT / 'out' / name):
        if candidate.is_file():
            return candidate
    return None


# Linux fork: each game's saves in its own folder of the saves directory, by save name prefix
# (runtime_savedata.c), whichever executable (the collection menu or the game) mounts them.
SAVE_FOLDERS = {'SAVEDATA01': 'Uncharted 1', 'SAVEDATA02': 'Uncharted 2', 'SAVEDATA03': 'Uncharted 3'}


def split_saves(user_dir):
    """Moves saves from the shared savedata/<user>/<title>/ into the per-game folders, once."""
    import shutil
    for title in user_dir.glob('savedata/*/*'):
        if not title.is_dir() or title.name.endswith('.memory'):
            continue
        for entry in sorted(title.iterdir()):
            prefix = entry.name[:10]
            if prefix not in SAVE_FOLDERS:
                continue
            target = user_dir / SAVE_FOLDERS[prefix] / entry.name
            if target.exists():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(entry), str(target))
            print(f'Saves: moved {entry.name} to {target.parent.name}/')


def executable_name(value):
    return EXECUTABLES.get(value.lower(), value) if value else EXECUTABLES['u1']


def locate(env, name):
    """(folder, executable file) for one of the collection's executables. A game dumped on its own
    (BB_GAME_DIR_U1/U2/U3) comes from its folder, where the game may be that folder's eboot.bin."""
    key = next((k for k, v in EXECUTABLES.items() if v == name), '')
    own = env.get(f'BB_GAME_DIR_{key.upper()}', '') if key else ''
    folder = own or env.get('BB_GAME_DIR', '')
    if not folder:
        return None, name
    folder = Path(folder)
    if own and not (folder / name).is_file() and (folder / 'eboot.bin').is_file():
        name = 'eboot.bin'
    return folder, name


def prepare(game, exe, out):
    """boot-linked.bin for one executable, kept until the executable or the scripts change."""
    out.mkdir(parents=True, exist_ok=True)
    linked = out / 'boot-linked.bin'
    # Which executable the files in out/ were made from: another folder (a per-game one) needs a new run.
    stamp, source = out / 'source.txt', str(game / exe)
    sources = [game / exe, *(PORT / 'scripts').glob('*.py'), *(game / 'sce_module').glob('*.prx')]
    if linked.is_file() and all(linked.stat().st_mtime >= s.stat().st_mtime for s in sources):
        if not stamp.is_file():
            stamp.write_text(source, encoding='utf-8')
        if stamp.read_text(encoding='utf-8', errors='replace') == source:
            return
    print(f'Preparing {exe} ...')
    run_script('prepare.py', game, '--out', out, '--exe', exe)
    run_script('link_modules.py', game, '--out', out, '--exe', exe)
    stamp.write_text(source, encoding='utf-8')


def config_value(env, name):
    """A setting of bbport.ini (the in-game menu and the launchers write it), read again at each
    start so "Save and restart" picks it up; None when absent (the last line of a key wins)."""
    try:
        text = Path(env.get('BB_CONFIG', '')).read_text(encoding='utf-8', errors='replace')
    except OSError:
        return None
    value = None
    for line in text.splitlines():
        key, _, rest = line.partition('=')
        if key.strip() == name:
            value = rest.strip()
    return value


def config_flag(env, name):
    """A 0/1 setting of bbport.ini (config_value)."""
    return config_value(env, name) == '1'


def ini_upscale_preset(env, key=''):
    """ARM port: the upscaling preset of bbport.ini (the ARM launcher and the in-game menu:
    preset=1..4 with a temporal upscaler, upscaler= or BB_UPSCALER): quality, balanced, performance
    or ultra, '' for none. It sets the render size instead of render_720p / render_540p.
    BB_UPSCALE_QUALITY[_U1/_U2/_U3] (the x86-64 launcher) is read by render_resolution()."""
    if (env.get(f'BB_UPSCALE_QUALITY_{key.upper()}') if key else '') or env.get('BB_UPSCALE_QUALITY'):
        return ''
    upscaler = (env.get('BB_UPSCALER') or config_value(env, 'upscaler') or '').strip().lower()
    if upscaler not in TEMPORAL_UPSCALERS:
        return ''
    return {'1': 'quality', '2': 'balanced', '3': 'performance', '4': 'ultra'}.get(
        config_value(env, 'preset') or '', '')


def render_720p(env, key=''):
    """BB_RENDER_720P (the launcher's Steam Deck mode), else the in-game menu's choice in bbport.ini
    (render_720p=1, ARM port), unless a bbport.ini upscaling preset sets the size."""
    if env.get('BB_RENDER_720P') in ('0', '1'):
        return env['BB_RENDER_720P'] == '1'
    if ini_upscale_preset(env, key):
        return False
    return config_flag(env, 'render_720p')


# Upscaling presets (BB_UPSCALE_QUALITY[_U1/_U2/_U3]): the resolution chosen (BB_RESOLUTION) is the
# output; the game draws at it divided by the preset's scale, and DLSS / FSR 4 / FSR 3.1 (or TAA
# with FSR 1) bring each frame up to it before the HUD and menus are drawn at the output size
# (TemporalUpscaler::OnComposite). After BilalReloaded's Uncharted PC port.
UPSCALE_SCALES = {'quality': 1.5, 'balanced': 1.7, 'performance': 2.0, 'ultra': 3.0}
# ARM port: the launcher's upscaling output 1280x720 (BB_UPSCALE_OUTPUT) with a bbport.ini preset:
# the render sizes the Android launcher uses for that output (960x540 for Quality, as FSR 1's 540p).
UPSCALE_720_SIZES = {'quality': (960, 540), 'balanced': (832, 468), 'performance': (640, 360), 'ultra': (640, 360)}
TEMPORAL_UPSCALERS = ('fsr3', 'gsr2', 'fsr4', 'fsr411', 'dlss')
COMPOSITE_PS = '749bbec9'  # the copy of the finished frame into the display buffer (all three games)
TONEMAP_PS = '484cdbf5'    # the draw finishing the frame (Uncharted 3 draws prompts into it after)


def parse_size(text):
    try:
        width, height = (int(value) for value in text.lower().split('x'))
        return width, height
    except ValueError:
        return None


def render_resolution(env, exe, out, image):
    """The image to start for the chosen resolution (BB_RESOLUTION[_U1/_U2/_U3]=WxH) and upscaling
    preset (BB_UPSCALE_QUALITY[_U1/_U2/_U3]): boot-linked.bin patched by scripts/render_res.py for
    the size the game draws at, kept until it or the scripts change, and the environment for it:
    the scene size (anti-aliasing, scene depth), the movie size (cutscene movies scaled to it),
    the direct memory the larger render target heaps need, and, when the game draws below the
    resolution chosen, the upscale to it. 1920x1080 without a preset: the image as it is."""
    key = next((k for k, v in EXECUTABLES.items() if v == exe), '')
    for name in ('BB_SCENE_SIZE', 'BB_MOVIE_SIZE', 'BB_VDEC_CONTENT', 'BB_RENDER_RES', 'BB_OUTPUT_RES',
                 'BB_UI_COMPOSITE_PS', 'BB_UI_TONEMAP_PS'):
        env.pop(name, None)
    if 'BB_DMEM_MB_USER' in env:
        env['BB_DMEM_MB'] = env['BB_DMEM_MB_USER']
    else:
        env.pop('BB_DMEM_MB', None)
    if not key:
        return image
    # The 720p mode (Steam Deck mode, ARM's render_720p) renders at 720p its own way
    # (scripts/render_720p.py).
    if render_720p(env, key):
        if env.get(f'BB_RESOLUTION_{key.upper()}') or env.get('BB_RESOLUTION'):
            print('Render resolution: not used with the 720p mode (the game renders at 1280x720)')
        return image
    size = (env.get(f'BB_RESOLUTION_{key.upper()}') or env.get('BB_RESOLUTION', '')).strip().lower()
    # 960x540 (bbport.ini render_540p: the in-game menu and the ARM launcher): the whole game at
    # that size, scaled to the screen like the 720p mode, whatever resolution the launcher sends.
    ini_preset = ini_upscale_preset(env, key)
    preset = ini_preset or (env.get(f'BB_UPSCALE_QUALITY_{key.upper()}') or
                            env.get('BB_UPSCALE_QUALITY', '')).strip().lower()
    render_540p = config_flag(env, 'render_540p') and not ini_preset
    if render_540p:
        if size and size != '960x540':
            print(f'Render resolution: 960x540 (render_540p) instead of {size}')
        size = '960x540'
    output = parse_size(size) if size not in ('', 'native', 'off', '0') else None
    output = output or (1920, 1080)
    scale = None if render_540p else UPSCALE_SCALES.get(preset)
    if scale and env.get('BB_UPSCALER', '').lower() == 'none':
        print('Upscaling preset: needs the camera tracking (BB_UPSCALER=none): drawing at the output size')
        scale = None
    wanted = output
    if scale and ini_preset and env.get('BB_UPSCALE_OUTPUT', '').strip().lower() == '1280x720':
        # The ARM launcher's 1280x720 output: FSR 3.1 / GSR 2 rebuild 720p frames, the screen's
        # scaling does the rest (less GPU time than rebuilding 1080p ones).
        output = (1280, 720)
        wanted = UPSCALE_720_SIZES[preset]
    elif scale:
        # Widths in steps of 16 (Uncharted 3 asserts it in its post processing), heights of 4.
        wanted = (max(640, round(output[0] / scale / 16) * 16), max(360, round(output[1] / scale / 4) * 4))
    render = wanted
    patched = image
    movie_decoder = False
    if wanted != (1920, 1080):
        import json
        text = f'{wanted[0]}x{wanted[1]}'
        patched = out / f'boot-res-{text}.bin'
        meta = patched.with_suffix('.json')
        sources = [image, PORT / 'scripts' / 'render_res.py']
        if not (patched.is_file() and meta.is_file() and
                all(patched.stat().st_mtime >= s.stat().st_mtime for s in sources)):
            print(f'Render resolution: preparing {text} for {key.upper()} ...')
            for old in out.glob('boot-res-*'):
                old.unlink(missing_ok=True)
            run_script('render_res.py', image, '--out', patched, '--exe', key, '--size', text)
        info = json.loads(meta.read_text(encoding='utf-8'))
        render = parse_size(info['render'])
        movie_decoder = bool(info.get('movie_decoder'))
        if info.get('extra_mib'):
            needed = 5056 + int(info['extra_mib']) + 64
            player = env.get('BB_DMEM_MB_USER', '')
            env['BB_DMEM_MB'] = str(max(needed, int(player)) if player.isdigit() else needed)
    render_text = f'{render[0]}x{render[1]}'
    env['BB_SCENE_SIZE'] = render_text
    if render_540p and render == (960, 540):
        # 540p: the movies stay 1920x1080 frames with the picture in their top-left 960x540, the
        # part the game shows (Videodec2, from revvv991's uncharted-android). The movie decoder
        # patched to 960x540 (render_res.py --movie-decoder) gave grey movies with coloured stripes.
        env['BB_VDEC_CONTENT'] = render_text
    elif render != (1920, 1080) and not movie_decoder:
        env['BB_MOVIE_SIZE'] = render_text
    upscaled = render[0] < output[0] or render[1] < output[1]
    if upscaled:
        env['BB_RENDER_RES'] = render_text
        env['BB_OUTPUT_RES'] = f'{output[0]}x{output[1]}'
        env['BB_UI_COMPOSITE_PS'] = COMPOSITE_PS
        env['BB_UI_TONEMAP_PS'] = TONEMAP_PS
    print(f'Render resolution: {key.upper()} draws {render_text}'
          f'{", upscaled to " + env["BB_OUTPUT_RES"] + (" (" + preset + ")" if scale else "") if upscaled else ""}'
          f', direct memory {env.get("BB_DMEM_MB", "5056")} MiB')
    return patched


def main():
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == '--after':
        wait_for(int(args[1]))
        args = args[2:]
    os.chdir(PORT)
    # Paths and titles can hold characters the console code page (GBK, ...) lacks: never fail on a print.
    # Pipes (the launcher reads them as UTF-8) get UTF-8; a real console keeps its own encoding.
    for stream in (sys.stdout, sys.stderr):
        if stream:
            stream.reconfigure(errors='replace', **({} if stream.isatty() else {'encoding': 'utf-8'}))
    env = os.environ
    # Game titles carry "™": the scripts must not fall back to the Windows code page.
    env['PYTHONUTF8'] = '1'
    env['PYTHONIOENCODING'] = 'utf-8'
    data = Path(env.get('BB_DATA_DIR', PORT))
    out_root = data / 'out'
    env.setdefault('BB_CONFIG', str(data / 'bbport.ini'))
    # Keyboard, mouse and controller bindings (the launcher's Controls page).
    env.setdefault('BB_INPUT_CONFIG', str(data / 'input.ini'))
    # The collection's renderer (all three games): the camera velocity pass of the motion blur
    # (its constants give the motion vectors), the compute pass after the scene is lit, before
    # the post-processing, where DLSS/FSR/TAA and frame generation take it, and the pass that
    # writes the finished picture before the UI (docs/upscaler.md).
    env.setdefault('BB_VELOCITY_PS', 'f4b18264')
    env.setdefault('BB_UPSCALE_BEFORE_CS', 'aec539f0')
    env.setdefault('BB_HUDLESS_PS', '484cdbf5')
    if not env.get('BB_FSR411_DIR') and not (PORT / 'fsr4_411').is_dir() and (data / 'fsr4_411').is_dir():
        env['BB_FSR411_DIR'] = str(data / 'fsr4_411')

    probe = Path(env['BB_PROBE']) if env.get('BB_PROBE') else find_executable(PROBE)
    if not probe or not probe.is_file():
        fail(f'{PROBE} not found: build it with build.sh or use a packaged build.')
    if not WINDOWS:
        # Linux package: bundled libraries in bin/lib. A frozen launcher (PyInstaller) points
        # LD_LIBRARY_PATH at its own libraries; the game gets the original value back.
        original = env.pop('LD_LIBRARY_PATH_ORIG', None)
        if getattr(sys, 'frozen', False):
            env.pop('LD_LIBRARY_PATH', None)
            if original:
                env['LD_LIBRARY_PATH'] = original
        if (probe.parent / 'lib').is_dir():
            env['LD_LIBRARY_PATH'] = os.pathsep.join(filter(None, [str(probe.parent / 'lib'), env.get('LD_LIBRARY_PATH')]))
    # Development builds run from the MSYS2 tree: its CLANG64 DLLs (SDL3, FFmpeg, ...).
    elif not (probe.parent / 'SDL3.dll').is_file():
        clang64 = Path(env.get('MSYS2_ROOT', r'C:\msys64')) / 'clang64' / 'bin'
        if clang64.is_dir():
            env['PATH'] = f'{clang64}{os.pathsep}{env.get("PATH", "")}'

    exe = executable_name(env.get('BB_EXE', ''))
    user_dir = Path(env.get('BB_USER_DIR', data / 'user')).resolve()
    user_dir.mkdir(parents=True, exist_ok=True)
    if not WINDOWS and env.get('BB_SAVEDATA_DIRS') is None:
        env['BB_SAVEDATA_DIRS'] = ';'.join(f'{k}={v}' for k, v in SAVE_FOLDERS.items())
        split_saves(user_dir)
    # GPU caches (shaders, pipelines) beside the saves; read before main(), so set here.
    env.setdefault('BB_GPU_USER_DIR', str(user_dir))
    env.setdefault('BB_VBLANK_HZ', '60')
    # A direct memory size the player set stays theirs; the render resolution raises it per game.
    if env.get('BB_DMEM_MB') and 'BB_DMEM_MB_USER' not in env:
        env['BB_DMEM_MB_USER'] = env['BB_DMEM_MB']
    loadexec = out_root / 'loadexec.txt'
    env['BB_LOADEXEC_FILE'] = str(loadexec)
    guest_argv = None
    if not WINDOWS:  # Linux fork: what ran, at the top of every log (crash reports read it)
        try:
            build = (PORT / 'LINUX_BUILD').read_text().strip() or 'source'
        except OSError:
            build = 'source'
        print(f'Run: Uncharted Linux {build}, game {exe}, {os.uname().sysname} {os.uname().release}, '
              f'vblank {env.get("BB_VBLANK_HZ")} Hz, window {env.get("BB_WINDOW_SIZE", "-")}, '
              f'extra {" ".join(k + "=" + env[k] for k in ("BB_NAN_GUARD", "BB_GUEST_MXCSR", "BB_PROBE", "BB_READBACKS", "BB_ASC_DRAIN", "BB_GPU_MERGE") if env.get(k)) or "-"}')
    # Linux fork, experiment: BB_U1_DIRECT=1 starts eboot.bin the way the collection menu starts
    # a game (arguments eboot.bin ispartofuccollection), to boot Uncharted 1 past the menu.
    if exe == EXECUTABLES['u1'] and env.get('BB_U1_DIRECT') == '1':
        guest_argv = out_root / 'guest-argv.txt'
        out_root.mkdir(parents=True, exist_ok=True)
        guest_argv.write_text('/app0/eboot.bin\neboot.bin\nispartofuccollection\n', encoding='utf-8')
        print('Starting eboot.bin as part of the collection (BB_U1_DIRECT=1)')
    # The game starts at the collection's menu (choose Uncharted 1, 2 or 3 there).
    # BB_SKIP_COLLECTION_MENU=1 opens Uncharted 1's own menu instead (probe.c
    # patch_collection_skip, from Mevlock's uncharted_pc-dev), for this first start only.
    # Uncharted 2 reads the results of GPU work (grenade explosions, vehicle damage, the convoy
    # of chapter 21) from memory it also writes: merging leaves them on the GPU, so it runs with
    # accurate GPU memory (BB_GPU_MERGE=0) unless the player chose otherwise. Uncharted 3 runs the
    # same engine and showed the same floating NPCs (Linux fork), so it does too.
    # Uncharted 2's aim and turret blur read tiny render results back (BB_READBACK_SMALL): U2
    # only, as upstream uncharted_pc 0.3.8 and the Android app (~13% slower in U3, upstream).
    player_merge = env.get('BB_GPU_MERGE')
    player_small = env.get('BB_READBACK_SMALL')
    player_copy64 = {name for name in ('BB_SCALAR_COPY64_SYNC', 'BB_SCALAR_COPY64_DEFER') if name in env}
    while True:
        if player_merge is None:
            if exe in (EXECUTABLES['u2'], EXECUTABLES['u3']):
                env['BB_GPU_MERGE'] = '0'
            else:
                env.pop('BB_GPU_MERGE', None)
        if player_small is None:
            env['BB_READBACK_SMALL'] = '1' if exe == EXECUTABLES['u2'] else '0'
        # Uncharted 2's mesh raycasters read GPU results on the CPU after checkpoints (copy
        # shader 0x6a6422a9): copied into guest memory, on a landing thread once the GPU is past
        # them (BB_SCALAR_COPY64_SYNC=2, BB_SCALAR_COPY64_DEFER=1; U2 only). Without it the game
        # asserted iSubMesh < m_numSubMeshInstances (erickmendes22's uncharted-android).
        for name, u2_value in (('BB_SCALAR_COPY64_SYNC', '2'), ('BB_SCALAR_COPY64_DEFER', '1')):
            if name not in player_copy64:
                env[name] = u2_value if exe == EXECUTABLES['u2'] else '0'
        game, file = locate(env, exe)
        if not game:
            fail('Set BB_GAME_DIR to the game folder (the one with eboot.bin).')
        if not (game / file).is_file():
            fail(f'No {file} in {game}.')
        out = out_root / Path(exe).stem.split('-')[0]
        prepare(game, file, out)
        image = render_resolution(env, exe, out, out / 'boot-linked.bin')
        run_script('content_profile.py', game, '--out', out, '--sku', env.get('BB_CONTENT_SKU', 'full'))
        this = ['--run'] if getattr(sys, 'frozen', False) else [str(Path(__file__).resolve())]
        env['BB_EXE'] = exe
        env['BB_RESTART_COMMAND'] = (subprocess.list2cmdline if WINDOWS else shlex.join)([sys.executable, *this, '--after', str(os.getpid()), *args])
        command = [str(probe), str(image), '--content-profile', str(out / 'content.bin'),
                   '--app0', str(game), '--user', str(user_dir), '--timeout', env.get('BB_TIMEOUT', '0')]
        if guest_argv:
            command += ['--guest-argv', str(guest_argv)]
        # Linux fork: BB_RENDER_720P=1 (Steam Deck mode) renders the game at 1280x720 through its
        # own 720p video settings (scripts/render_720p.py). An executable whose settings are not
        # found starts at its usual 1080p.
        if render_720p(env, next((k for k, v in EXECUTABLES.items() if v == exe), '')):
            patches = out / 'render-720p.bin'
            script = ['--script'] if getattr(sys, 'frozen', False) else []
            made = subprocess.run([sys.executable, *script, str(PORT / 'scripts' / 'render_720p.py'),
                                   str(out / 'eboot.elf'), str(patches)],
                                  stdin=subprocess.DEVNULL, creationflags=no_console()).returncode == 0
            if made:
                command += ['--patches', str(patches)]
            else:
                print('Render 720p: not available for this executable; rendering at 1080p')
        # ARM port: the launcher's graphics patches (BB_GRAPHICS_PATCHES=id ..., from the Bachata S4
        # catalog): the ones whose original bytes match this executable (scripts/graphics_patches.py),
        # applied after the others; a write that does not fit is skipped (--extra-patches).
        graphics = env.get('BB_GRAPHICS_PATCHES', '').split()
        # Below 1080p with the movies in a corner of 1080p frames (BB_VDEC_CONTENT, 540p): the
        # game's movie textures and chroma plane at the decoder's 1920x1088 (the catalog's
        # movies_below_1080p); without them the movies were green and enlarged.
        if env.get('BB_VDEC_CONTENT'):
            graphics.append('movies_below_1080p')
        # FSR 3.1, GSR 2 and TAA take the camera from the motion blur's velocity pass, which
        # no_motion_blur removes: they would run on no frame. no_aa removes the game's final frame
        # copy (pixel shader 749bbec9) the upscale starts from, and the velocity pass with it: the
        # presets drew black frames. The upscalers are the anti-aliasing then.
        upscaler = (env.get('BB_UPSCALER') or config_value(env, 'upscaler') or 'off').strip().lower()
        if upscaler in TEMPORAL_UPSCALERS + ('taa',):
            for patch in ('no_motion_blur', 'no_aa'):
                if patch in graphics:
                    graphics.remove(patch)
                    print(f'Graphics patch {patch}: not applied with the {upscaler} upscaler '
                          '(it needs the game\'s camera pass and frame copy)')
        if graphics:
            patches = out / 'graphics.bin'
            script = ['--script'] if getattr(sys, 'frozen', False) else []
            made = subprocess.run([sys.executable, *script, str(PORT / 'scripts' / 'graphics_patches.py'),
                                   str(out / 'eboot.elf'), exe, str(patches), *graphics],
                                  stdin=subprocess.DEVNULL, creationflags=no_console()).returncode == 0
            if made:
                command += ['--extra-patches', str(patches)]
        loadexec.unlink(missing_ok=True)
        # No stdin: an inherited pipe (shells such as Git Bash) cost the game its console output.
        code = subprocess.run([*command, *args], stdin=subprocess.DEVNULL, creationflags=no_console()).returncode
        if code != LOADEXEC_EXIT or not loadexec.is_file():
            return code
        lines = loadexec.read_text(encoding='utf-8', errors='replace').splitlines()
        target = Path(lines[0].replace('\\', '/')).name if lines else ''
        if not target:
            fail('The game asked for another executable without a name.')
        print(f'Switching to {target}')
        exe = target
        if env.get('BB_SKIP_COLLECTION_MENU') == '1':
            env['BB_SKIP_COLLECTION_MENU'] = '0'
        guest_argv = out_root / 'guest-argv.txt'
        guest_argv.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        # ARM port: Uncharted 2 started by the collection menu (as part of the collection: no
        # logos, straight to its menu) stayed on a grey or white screen on the Odin 3 at 1280x720,
        # and from the AppImage at 1080p too, while the same game started on its own (logos first)
        # runs. Until that is understood, a game the menu starts at 720p, or on an ARM host, starts
        # on its own; BB_KEEP_COLLECTION_FLAG=1 keeps the menu's arguments.
        if (target != 'eboot.bin' and 'ispartofuccollection' in lines[1:]
                and (render_720p(env) or platform.machine().lower() in ('aarch64', 'arm64'))
                and env.get('BB_KEEP_COLLECTION_FLAG') != '1'):
            print(f'Render 720p: starting {target} on its own (intro logos first), not as part of the collection')
            guest_argv = None


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
