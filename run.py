#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepares an Uncharted executable of the Nathan Drake Collection and starts bb-probe.exe.

BB_GAME_DIR   the game folder (CUSA02343, CUSA02320, ...; the one with eboot.bin)
BB_GAME_DIR_U1 / _U2 / _U3   optional: the folder of a game that was dumped on its own; used for
              that game instead of BB_GAME_DIR (its executable may be that folder's eboot.bin)
BB_EXE        u1 / u2 / u3 or an executable name (default u1: eboot.bin, the collection menu)
BB_USER_DIR   saves and shader caches (default user/ beside this script)
Extra arguments go to bb-probe. When the game starts another executable of the collection
(sceSystemServiceLoadExec), that one is prepared and started in its place.
The in-game "Apply and restart" runs this script again through BB_RESTART_COMMAND;
`--after PID` waits for the previous game process to end first (its GPU device and memory).
"""
import ctypes
import os
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
    # Linux fork: Uncharted 1 opens its own menu, not the collection's (probe.c
    # patch_collection_skip, from Mevlock's uncharted_pc-dev). Only for this first start: a game
    # that returns to the collection later (LoadExec into eboot.bin) shows it.
    # BB_SKIP_COLLECTION_MENU=0 keeps the collection menu.
    if exe == EXECUTABLES['u1'] and 'BB_SKIP_COLLECTION_MENU' not in env and not guest_argv:
        env['BB_SKIP_COLLECTION_MENU'] = '1'
    # Uncharted 2 reads the results of GPU work (grenade explosions, vehicle damage, the convoy
    # of chapter 21) from memory it also writes: merging leaves them on the GPU, so it runs with
    # accurate GPU memory (BB_GPU_MERGE=0) unless the player chose otherwise.
    player_merge = env.get('BB_GPU_MERGE')
    while True:
        if player_merge is None:
            if exe == EXECUTABLES['u2']:
                env['BB_GPU_MERGE'] = '0'
            else:
                env.pop('BB_GPU_MERGE', None)
        game, file = locate(env, exe)
        if not game:
            fail('Set BB_GAME_DIR to the game folder (the one with eboot.bin).')
        if not (game / file).is_file():
            fail(f'No {file} in {game}.')
        out = out_root / Path(exe).stem.split('-')[0]
        prepare(game, file, out)
        run_script('content_profile.py', game, '--out', out, '--sku', env.get('BB_CONTENT_SKU', 'full'))
        this = ['--run'] if getattr(sys, 'frozen', False) else [str(Path(__file__).resolve())]
        env['BB_EXE'] = exe
        env['BB_RESTART_COMMAND'] = (subprocess.list2cmdline if WINDOWS else shlex.join)([sys.executable, *this, '--after', str(os.getpid()), *args])
        command = [str(probe), str(out / 'boot-linked.bin'), '--content-profile', str(out / 'content.bin'),
                   '--app0', str(game), '--user', str(user_dir), '--timeout', env.get('BB_TIMEOUT', '0')]
        if guest_argv:
            command += ['--guest-argv', str(guest_argv)]
        # Linux fork: BB_RENDER_720P=1 (Steam Deck mode) renders the game at 1280x720 through its
        # own 720p video settings (scripts/render_720p.py). An executable whose settings are not
        # found starts at its usual 1080p.
        if env.get('BB_RENDER_720P') == '1':
            patches = out / 'render-720p.bin'
            script = ['--script'] if getattr(sys, 'frozen', False) else []
            made = subprocess.run([sys.executable, *script, str(PORT / 'scripts' / 'render_720p.py'),
                                   str(out / 'eboot.elf'), str(patches)],
                                  stdin=subprocess.DEVNULL, creationflags=no_console()).returncode == 0
            if made:
                command += ['--patches', str(patches)]
            else:
                print('Render 720p: not available for this executable; rendering at 1080p')
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


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
