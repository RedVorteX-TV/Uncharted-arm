"""Linux fork: the launcher's own log and bug reports.

Every start of the launcher (its window, a game started from Steam) writes <data>/logs/launcher.log
and keeps the previous ones as launcher.1.log ... launcher.4.log. A log begins with a summary of
the system and then gets everything the launcher and the libraries it loads print, Python errors,
the stack of a hard crash (faulthandler) and, when the window stops responding for HANG_SECONDS,
what it was doing. create_report() puts these logs, the game's run logs, the system details and
the settings into one zip for a bug report, with the home folder and user name taken out."""
import atexit
import faulthandler
import getpass
import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
import zipfile
from datetime import datetime
from pathlib import Path

KEEP = 5
HANG_SECONDS = 10
REPORT_LOG_BYTES = 8 << 20  # per log in a bug report
CLOSED = '== Launcher closed'
NOT_RESPONDING = '== Not responding'
# The settings shown in a log's summary (the report has the whole settings file).
SUMMARY_SETTINGS = ('steam_deck', 'frame_rate', 'window_size', 'render_resolution', 'upscale_preset',
                    'present_mode', 'fullscreen', 'anti_aliasing', 'frame_generation', 'extra_env',
                    'check_updates')

_log = None
_started = time.monotonic()
_beat = time.monotonic()
_watching = False
_auto_report = None


class Tee:
    """sys.stdout / sys.stderr: the log, and the terminal when the launcher was started from one."""
    encoding = 'utf-8'

    def __init__(self, console, log):
        self.console, self.log = console, log

    def write(self, text):
        for stream in (self.console, self.log):
            if stream is not None:
                try:
                    stream.write(text)
                    stream.flush()
                except (OSError, ValueError):
                    pass
        return len(text)

    def flush(self):
        pass

    def isatty(self):
        return self.console is not None and self.console.isatty()


def log_dir(data_dir):
    return Path(data_dir) / 'logs'


def rotate(folder, name='launcher', keep=KEEP):
    """launcher.log -> launcher.1.log ... launcher.<keep-1>.log."""
    for n in range(keep - 1, 0, -1):
        newer = folder / (f'{name}.log' if n == 1 else f'{name}.{n - 1}.log')
        if newer.is_file():
            try:
                os.replace(newer, folder / f'{name}.{n}.log')
            except OSError:
                pass


def start_session(data_dir, version, settings, role='window'):
    """Starts this run's launcher.log. Output written straight to the file descriptors (Tk, X11,
    fontconfig) reaches the log too, unless the launcher runs in a terminal, which keeps it."""
    global _log
    folder = log_dir(data_dir)
    try:
        folder.mkdir(parents=True, exist_ok=True)
        rotate(folder)
        _log = open(folder / 'launcher.log', 'w', encoding='utf-8', errors='replace', buffering=1)
    except OSError:
        return None
    terminal = sys.__stderr__ is not None and sys.__stderr__.isatty()
    if not terminal:
        for fd in (1, 2):
            try:
                os.dup2(_log.fileno(), fd)
            except OSError:
                pass
    sys.stdout = Tee(sys.__stdout__ if terminal else None, _log)
    sys.stderr = Tee(sys.__stderr__ if terminal else None, _log)
    faulthandler.enable(file=_log, all_threads=True)
    _log.write(f'== Uncharted launcher {version} ({role}), {datetime.now():%Y-%m-%d %H:%M:%S}\n')
    _log.write(system_summary(settings) + '\n')
    atexit.register(_closed)
    try:
        signal.signal(signal.SIGTERM, _terminated)
    except (OSError, ValueError):
        pass
    return folder / 'launcher.log'


def console():
    """The terminal the launcher was started from, or None (game output then stays out of the
    launcher's log: it has its own)."""
    stream = sys.stdout
    return stream.console if isinstance(stream, Tee) else stream


def note(text):
    if _log is not None:
        _log.write(f'[{time.monotonic() - _started:7.1f}s] {text}\n')


def _closed():
    if _log is not None:
        note(f'{CLOSED} normally')
        _log.flush()


def _terminated(_signum, _frame):
    if _auto_report is not None:
        _auto_report()
    if _log is not None:
        note(f'{CLOSED} by the system (SIGTERM)')
        _log.flush()
    os._exit(143)


def logged_dialogs(messagebox):
    """tkinter.messagebox with every dialog in the log, when it opens and how it was answered (a
    dialog hidden behind the window otherwise looks like a hang)."""
    import types

    def wrap(name, show):
        def logged(title=None, message=None, **options):
            note(f'Dialog {name}: {str(message or "")[:200]!r}')
            answer = show(title, message, **options)
            note(f'Dialog {name} answered: {answer!r}')
            return answer
        return logged
    return types.SimpleNamespace(**{name: wrap(name, getattr(messagebox, name)) if name.startswith(('show', 'ask'))
                                    else getattr(messagebox, name) for name in dir(messagebox) if not name.startswith('_')})


def auto_report(make, after=20):
    """Test builds (an AUTO_REPORT file in the package): a bug report without being asked, once:
    when the window stops responding, `after` seconds after the start, or when the launcher
    closes, whichever comes first. For players whose window does not work at all."""
    global _auto_report
    done = threading.Lock()

    def once():
        if done.acquire(blocking=False):
            note('Automatic bug report (test build)')
            try:
                make()
            except Exception as error:  # the launcher goes on without it
                note(f'Automatic bug report failed: {error}')
    _auto_report = once
    timer = threading.Timer(after, once)
    timer.daemon = True
    timer.start()
    atexit.register(once)


# ---- The window: is it still responding? ------------------------------------------------------

def watch_window():
    """Starts the watchdog before the window is built: a start that hangs is caught as well."""
    global _watching
    if _log is None or _watching:
        return
    _watching = True
    beat()
    threading.Thread(target=_watchdog, daemon=True, name='launcher-watchdog').start()


def heartbeat(root):
    """Every second from Tk's event loop: proof the window still responds."""
    beat()
    root.after(1000, heartbeat, root)


def beat():
    global _beat
    _beat = time.monotonic()


def _watchdog():
    stalled = False
    while True:
        time.sleep(1)
        late = time.monotonic() - _beat
        if late >= HANG_SECONDS and not stalled:
            stalled = True
            note(f'{NOT_RESPONDING} for {late:.0f} s; what the launcher is doing:')
            faulthandler.dump_traceback(file=_log, all_threads=True)
            if _auto_report is not None:
                _auto_report()
        elif late < 2 and stalled:
            stalled = False
            note('== Responding again')


# ---- What is this system? ---------------------------------------------------------------------

def read_value(path, key=None):
    try:
        for line in Path(path).read_text(errors='replace').splitlines():
            if key is None or line.startswith(key):
                return line.split('=', 1)[-1].split(':', 1)[-1].strip().strip('"')
    except OSError:
        pass
    return '?'


def gpus():
    """The graphics devices: PCI ids and kernel driver from sysfs, names from lspci when present."""
    found = []
    for card in sorted(Path('/sys/class/drm').glob('card[0-9]')):
        device = card / 'device'
        try:
            vendor = (device / 'vendor').read_text().strip()
            model = (device / 'device').read_text().strip()
            driver = Path(os.readlink(device / 'driver')).name
        except OSError:
            continue
        found.append(f'{card.name}: {vendor}:{model} driver {driver}')
    try:
        listing = subprocess.run(['lspci'], capture_output=True, text=True, timeout=5).stdout
        found += [line for line in listing.splitlines() if re.search(r'VGA|3D|Display', line)]
    except (OSError, subprocess.SubprocessError):
        pass
    return found or ['?']


def memory():
    total = read_value('/proc/meminfo', 'MemTotal')
    try:
        return f'{int(total.split()[0]) / 1048576:.1f} GB'
    except (ValueError, IndexError):
        return '?'


def system_summary(settings=None):
    glibc = os.confstr('CS_GNU_LIBC_VERSION') if hasattr(os, 'confstr') else '?'
    lines = [
        f'System: {read_value("/etc/os-release", "PRETTY_NAME")}, kernel {platform.release()}, {glibc}',
        f'CPU: {read_value("/proc/cpuinfo", "model name")}, {os.cpu_count()} threads, {memory()} RAM',
        f'Session: {os.environ.get("XDG_SESSION_TYPE", "?")}, desktop {os.environ.get("XDG_CURRENT_DESKTOP", "?")}, '
        f'DISPLAY={os.environ.get("DISPLAY", "")} WAYLAND_DISPLAY={os.environ.get("WAYLAND_DISPLAY", "")}'
        f'{", Gaming Mode (gamescope)" if os.environ.get("GAMESCOPE_WAYLAND_DISPLAY") else ""}',
        *(f'GPU: {line}' for line in gpus()),
        f'Launcher: {os.environ.get("APPIMAGE") or sys.executable}',
    ]
    if settings:
        shown = ', '.join(f'{key}={settings.get(key)!r}' for key in SUMMARY_SETTINGS if key in settings)
        lines.append(f'Settings: {shown}')
        for key in ('game_dir', 'user_dir'):
            folder = str(settings.get(key) or '').strip()
            if folder:
                lines.append(f'{key}: {folder} ({"found" if Path(folder).is_dir() else "MISSING"})')
    return '\n'.join(lines)


# ---- Health check and bug report ------------------------------------------------------------

def health(data_dir, saves_folder, crash_signature):
    """What the recent logs say went wrong, newest first, as short sentences."""
    found = []
    signature, _text = crash_signature(Path(saves_folder) / 'last_run.log')
    if signature:
        found.append(f'The last game run stopped with: {signature[:160]}')
    folder = log_dir(data_dir)
    for name, when in (('launcher.log', 'This launcher session'), ('launcher.1.log', 'The previous launcher session')):
        try:
            text = (folder / name).read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        if NOT_RESPONDING in text:
            found.append(f'{when} stopped responding for a while (the log has what it was doing).')
        if name != 'launcher.log':
            if 'Fatal Python error' in text or 'Traceback (most recent call last)' in text:
                found.append(f'{when} had a Python error.')
            if CLOSED not in text:
                found.append(f'{when} did not close normally (a crash, or it was forced to close).')
    try:
        free = shutil.disk_usage(data_dir).free
        if free < 5 << 30:
            found.append(f'Only {free / (1 << 30):.1f} GB free where the launcher keeps its data.')
    except OSError:
        pass
    return found


def private(text):
    """The home folder and the user name out of a text that goes into a report."""
    home = str(Path.home())
    if home and home != '/':
        text = text.replace(home, '~')
    try:
        user = getpass.getuser()
    except (KeyError, OSError):
        user = ''
    if len(user) >= 2:  # only in paths: a user called "deck" stays in "Steam Deck"
        text = re.sub(rf'/{re.escape(user)}(?=/|\b)', '/<user>', text)
    return text


def downloads_folder():
    try:
        result = subprocess.run(['xdg-user-dir', 'DOWNLOAD'], capture_output=True, text=True, timeout=5)
        folder = Path(result.stdout.strip())
        if result.returncode == 0 and folder != Path.home() and folder.is_dir():
            return folder
    except (OSError, subprocess.SubprocessError):
        pass
    folder = Path.home() / 'Downloads'
    return folder if folder.is_dir() else Path.home()


def run_quietly(command, timeout=20):
    try:
        result = subprocess.run(command, capture_output=True, text=True, errors='replace', timeout=timeout)
        return (result.stdout + result.stderr).strip() or f'(no output, exit {result.returncode})'
    except FileNotFoundError:
        return '(not installed)'
    except (OSError, subprocess.SubprocessError) as error:
        return f'({error})'


def create_report(data_dir, saves_folder, settings, port_dir, version, crash_signature, folder=None):
    """uncharted-report-<time>.zip in Downloads: the launcher's and the game's logs, the system,
    the Vulkan devices, the settings and the health check. Returns its path."""
    folder = Path(folder) if folder else downloads_folder()
    path = folder / f'uncharted-report-{datetime.now():%Y%m%d-%H%M%S}.zip'
    note(f'Creating a bug report: {path}')
    found = health(data_dir, saves_folder, crash_signature)
    capabilities = Path(port_dir) / 'bin' / 'bb-gpu-capabilities'
    system = '\n\n'.join([
        f'Uncharted launcher {version}, report of {datetime.now():%Y-%m-%d %H:%M:%S}',
        system_summary(settings),
        '== Vulkan (the game\'s check)\n' + (run_quietly([str(capabilities)]) if capabilities.is_file() else '(not found)'),
        '== vulkaninfo --summary\n' + run_quietly(['vulkaninfo', '--summary']),
        '== Disk space\n' + '\n'.join(f'{label}: {free_space(p)}' for label, p in
                                      (('Launcher data', data_dir), ('Saves folder', saves_folder))),
    ])
    health_text = '\n'.join(f'- {line}' for line in found) or 'No problems found in the recent logs.'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as report:
        report.writestr('system.txt', private(system) + '\n')
        report.writestr('health.txt', private(health_text) + '\n')
        report.writestr('settings.json', private(json.dumps(settings, indent=2, sort_keys=True, default=str)) + '\n')
        for source, prefix in ((log_dir(data_dir), 'launcher'), (Path(saves_folder), 'game')):
            for log in sorted(source.glob('launcher*.log' if prefix == 'launcher' else 'last_run*.log')):
                try:  # the start and the end of a run log that grew huge
                    size = log.stat().st_size
                    with open(log, 'rb') as file:
                        if size <= REPORT_LOG_BYTES:
                            text = file.read().decode('utf-8', errors='replace')
                        else:
                            head = file.read(REPORT_LOG_BYTES // 4).decode('utf-8', errors='replace')
                            file.seek(size - REPORT_LOG_BYTES * 3 // 4)
                            tail = file.read().decode('utf-8', errors='replace')
                            text = f'{head}\n\n[... {size / (1 << 20):.0f} MB log, middle left out ...]\n\n{tail}'
                except OSError:
                    continue
                report.writestr(f'{prefix}/{log.name}', private(text))
    return path


def free_space(folder):
    try:
        return f'{shutil.disk_usage(folder).free / (1 << 30):.1f} GB free'
    except OSError:
        return '?'


def tell(title, message):
    """A desktop notification or dialog without Tk (--report runs when the window may not work)."""
    for command in (['notify-send', '-a', title, title, message],
                    ['zenity', '--info', f'--title={title}', f'--text={message}'],
                    ['kdialog', '--title', title, '--msgbox', message]):
        if shutil.which(command[0]):
            try:
                subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
                return
            except OSError:
                continue
