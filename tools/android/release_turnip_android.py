#!/usr/bin/env python3
"""Package the Android (Bionic) Turnip builds of build_turnip_android.sh as adrenotools-style
driver zips, and optionally publish them as GitHub releases.

    release_turnip_android.py [main|gen8 ...] [--publish] [--repo OWNER/NAME]

Each zip is flat: libvulkan_freedreno.so, meta.json (the adrenotools fields plus the build's
provenance) and LICENSE-mesa.txt. Zips are deterministic: the same build gives the same bytes.
The patches the library was built with travel beside it in patches-android-*.tar.gz.
--publish creates the release turnip-android-<variant>-<version>-<sha> and never touches one
that exists (no --clobber).
"""
import argparse
import gzip
import hashlib
import io
import json
import re
import shlex
import subprocess
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out/bionic/turnip-release'
VARIANTS = {
    'main': {
        'dir': ROOT / 'out/bionic/turnip-main',
        'src': ROOT / '.local-deps/android/src/mesa-android-main',
        'name': 'Turnip Android main (Adreno 6xx/7xx)',
        'adreno': 'Adreno 6xx/7xx',
    },
    'gen8': {
        'dir': ROOT / 'out/bionic/turnip-gen8',
        'src': ROOT / '.local-deps/android/src/mesa-android-gen8',
        'name': 'Turnip Android gen8 (Adreno 8xx)',
        'adreno': 'Adreno 8xx',
    },
}
DEFAULT_REPO = 'ducvd89/Bloodborne-Android'
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(tree, *args):
    return subprocess.run(['git', '-C', str(tree), *args], check=True, capture_output=True,
                          text=True).stdout.strip()


def provenance(spec):
    """BUILD-INFO.json of the build, checked against the library next to it."""
    info_file = spec['dir'] / 'BUILD-INFO.json'
    if not info_file.is_file():
        raise SystemExit(f'No BUILD-INFO.json in {spec["dir"]}: build it with build_turnip_android.sh')
    info = json.loads(info_file.read_text())
    library = (spec['dir'] / info['library']).read_bytes()
    if info['librarySha256'] != sha256(library):
        raise SystemExit(f"{info['library']} is not the library BUILD-INFO.json describes; rebuild it")
    return info, library


def patch_subject(path):
    """The one-line description of a patch: its Subject: header (which may wrap), minus [PATCH]."""
    lines = (ROOT / path).read_text(errors='replace').splitlines()
    for number, line in enumerate(lines):
        if line.startswith('Subject:'):
            subject = line[len('Subject:'):].strip()
            for more in lines[number + 1:]:
                if not more.startswith((' ', '\t')):
                    break
                subject += ' ' + more.strip()
            return re.sub(r'^\[PATCH[^\]]*\]\s*', '', subject)
    return Path(path).stem


def license_text(src, commit):
    """Mesa's license files from the source tree, verbatim: docs/license.rst, then licenses/*."""
    if git(src, 'rev-parse', 'HEAD') != commit:
        raise SystemExit(f'{src} is not at {commit[:7]}: rebuild so its license text matches')
    names = ['docs/license.rst'] + sorted(
        str(f.relative_to(src)) for f in (src / 'licenses').rglob('*') if f.is_file())
    if not (src / names[0]).is_file() or len(names) < 2:
        raise SystemExit(f'No Mesa license files in {src}')
    return '\n\n'.join(f'==== {name} ====\n\n{(src / name).read_text()}' for name in names)


def commit_url(repository, commit):
    base = repository.removesuffix('.git')
    return f'{base}/-/commit/{commit}' if 'gitlab' in base else f'{base}/commit/{commit}'


def write_zip(path, files):
    """files: name -> bytes. Fixed times and modes, sorted names: the same input, the same zip."""
    with zipfile.ZipFile(path, 'w') as zf:
        for name in sorted(files):
            entry = zipfile.ZipInfo(name, ZIP_TIME)
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            zf.writestr(entry, files[name], compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def write_patches(path, info):
    """The exact patch files of BUILD-INFO.json, as the source that goes with the binary."""
    for patch in info['patches']:
        if sha256((ROOT / patch['file']).read_bytes()) != patch['sha256']:
            raise SystemExit(f"{patch['file']} changed since the build (sha256 differs from BUILD-INFO.json)")
    rel = lambda p: Path(p['file']).relative_to('tools/android')
    order = '\n'.join(f"{n}. {rel(p)}  sha256 {p['sha256']}" for n, p in enumerate(info['patches'], 1))
    text = (f"Turnip Android {info['variant']}, Mesa {info['mesaVersion']}\n"
            f"Repository: {info['mesaRepository']}\nCommit: {info['mesaCommit']}\n"
            f"Target: {info['target']}\n\n"
            f"Patches, in the order applied (git apply, from the Mesa root):\n{order}\n\n"
            f"patches/build_turnip_android.sh is the script that ran them and the meson build.\n")
    files = {'SOURCE.txt': text.encode(),
             'patches/build_turnip_android.sh': (ROOT / 'tools/android/build_turnip_android.sh').read_bytes()}
    for patch in info['patches']:
        files['patches/' + str(rel(patch))] = (ROOT / patch['file']).read_bytes()
    # Reproducible: sorted names, no times or owners, a gzip header without a name or time.
    buffer = io.BytesIO()
    with gzip.GzipFile(fileobj=buffer, mode='wb', mtime=0, filename='', compresslevel=9) as gz, \
            tarfile.open(fileobj=gz, mode='w', format=tarfile.GNU_FORMAT) as tar:
        for name in sorted(files):
            entry = tarfile.TarInfo(name)
            entry.size, entry.mode = len(files[name]), 0o644
            tar.addfile(entry, io.BytesIO(files[name]))
    path.write_bytes(buffer.getvalue())


def release_notes(spec, info, base, zip_sha, patches_name, patches_sha):
    patches = '\n'.join(f"- `{Path(p['file']).name}`: {patch_subject(p['file'])}" for p in info['patches'])
    return f"""# {spec['name']}: Mesa {info['mesaVersion']} ({info['mesaCommit'][:7]})

Turnip, Mesa's open-source Vulkan driver for Qualcomm {spec['adreno']} GPUs (KGSL kernel driver),
built with the Android NDK for the Bloodborne Android app's native (Bionic) game process. The
library is dlopen'ed directly and entered through `vk_icdGetInstanceProcAddr`; it needs only
Android system libraries (libc, libm, libdl, liblog, libnativewindow, libsync, libz).

## Install

In the app: menu, GPU driver, Download, or Install zip (pick `{base}.zip`). The zip is an
adrenotools-style driver package (`meta.json` + `{info['library']}`).

## Build

- GPUs: {spec['adreno']}
- Mesa: [{info['mesaRepository'].removesuffix('.git')} @ {info['mesaCommit'][:7]}]({commit_url(info['mesaRepository'], info['mesaCommit'])}), commit `{info['mesaCommit']}`, version {info['mesaVersion']}
- Target: {info['target']}
- Built: {info['buildDate']}

Patches applied on top, in order:

{patches}

Source: Mesa commit above + patches in `{patches_name}` (with `SOURCE.txt` giving the order).

## Checksums (SHA-256)

- `{base}.zip`: `{zip_sha}`
- `{patches_name}`: `{patches_sha}`
- `{info['library']}`: `{info['librarySha256']}`

Mesa is licensed under the MIT license (individual files may carry their own); the license texts are in
`LICENSE-mesa.txt` in the zip.
"""


def package(variant):
    spec = VARIANTS[variant]
    info, library = provenance(spec)
    version = re.sub(r'-devel$', '', info['mesaVersion'])
    short = info['mesaCommit'][:7]
    base = f'Turnip-android-{variant}-{version}-{short}'
    meta = {
        'schemaVersion': 1,
        'name': spec['name'],
        'description': (f"Mesa Turnip (Vulkan for Qualcomm {spec['adreno']}, KGSL) built with the Android NDK "
                        f"for the Bloodborne Android app's native process, with the app's patches."),
        'author': 'ducvd89',
        'packageVersion': '1',
        'vendor': 'Mesa',
        'driverVersion': f"Mesa {info['mesaVersion']} {short}",
        'minApi': 30,
        'libraryName': info['library'],
        'platform': 'android',
        'architecture': 'aarch64',
        'variant': variant,
        'mesa': {'repository': info['mesaRepository'], 'commit': info['mesaCommit'],
                 'version': info['mesaVersion']},
        'patches': info['patches'],
        'sha256': info['librarySha256'],
        'buildDate': info['buildDate'],
    }
    dump = lambda value: (json.dumps(value, indent=2) + '\n').encode()
    OUT.mkdir(parents=True, exist_ok=True)
    zip_path = OUT / f'{base}.zip'
    write_zip(zip_path, {
        info['library']: library,
        'meta.json': dump(meta),
        'LICENSE-mesa.txt': license_text(spec['src'], info['mesaCommit']).encode(),
    })
    zip_sha = sha256(zip_path.read_bytes())
    # Not Turnip-*.zip: a driver downloader picks that name, and this is not a driver.
    patches_path = OUT / f'patches-android-{variant}-{version}-{short}.tar.gz'
    write_patches(patches_path, info)
    patches_sha = sha256(patches_path.read_bytes())
    notes_path = OUT / f'{base}.md'
    notes_path.write_text(release_notes(spec, info, base, zip_sha, patches_path.name, patches_sha))
    print(f'{variant}: {zip_path} ({zip_path.stat().st_size:,} bytes), sha256 {zip_sha}')
    print(f'{variant}: {patches_path} ({patches_path.stat().st_size:,} bytes), sha256 {patches_sha}')
    return {'tag': f'turnip-android-{variant}-{version}-{short}',
            'title': f"Turnip Android {variant} {version} ({short}) - {spec['adreno']}",
            'zip': zip_path, 'patches': patches_path, 'notes': notes_path}


def publish(release, repo):
    probe = subprocess.run(['gh', 'release', 'view', release['tag'], '--repo', repo],
                           capture_output=True, text=True)
    if probe.returncode == 0:
        print(f"Release {release['tag']} already exists in {repo}; skipping")
        return
    if 'not found' not in probe.stderr.lower():
        raise SystemExit(f"gh release view failed: {probe.stderr.strip()}")
    head = git(ROOT, 'rev-parse', 'HEAD')
    command = ['gh', 'release', 'create', release['tag'], str(release['zip']), str(release['patches']),
               '--repo', repo, '--title', release['title'], '--notes-file', str(release['notes']),
               '--latest=false', '--target', head]
    print('+', shlex.join(command))
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('variants', nargs='*', metavar='{main,gen8}',
                        help='variants to package (default: main and gen8)')
    parser.add_argument('--publish', action='store_true', help='create the GitHub releases')
    parser.add_argument('--clobber', action='store_true', help='refused: published assets are never replaced')
    parser.add_argument('--repo', default=DEFAULT_REPO, help=f'GitHub repository (default {DEFAULT_REPO})')
    args = parser.parse_args()
    for variant in args.variants:
        if variant not in VARIANTS:
            parser.error(f'unknown variant {variant}')
    if args.clobber:
        parser.error('--clobber is refused: a published asset is never replaced')
    releases = [package(variant) for variant in args.variants or ['main', 'gen8']]
    if args.publish:
        for release in releases:
            publish(release, args.repo)


if __name__ == '__main__':
    main()
