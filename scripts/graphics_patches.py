"""Uncharted ARM port: the launcher's graphics patches (patches/graphics_patches.json, from the
Bachata S4+ catalog) as a BBPATCH2 file (src/probe.c --extra-patches).

graphics_patches.py ELF EXE OUT [ID ...]

ELF is the prepared executable (eboot.elf), EXE its name in the game folder (eboot.bin,
big2-ps4_Shipping.elf, big3-ps4_Shipping.elf). A patch is written only when the original bytes
of every one of its writes match the catalog's SHA-256 for exactly one game version; the others
are reported and left out. tools/android/prepare/graphics.cpp does the same on Android.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

CATALOG = Path(__file__).resolve().parent.parent / 'patches' / 'graphics_patches.json'
BASE = 0x400000  # the catalog's PS4 addresses; the image starts at vaddr 0
PT_LOAD, PT_SCE_RELRO = 1, 0x61000010


def segments(elf):
    if elf[:4] != b'\x7fELF':
        raise ValueError('not an ELF image')
    phoff, = struct.unpack_from('<Q', elf, 0x20)
    phnum, = struct.unpack_from('<H', elf, 0x38)
    out = []
    for i in range(phnum):
        kind, _, offset, vaddr, _, filesz = struct.unpack_from('<IIQQQQ', elf, phoff + i * 56)
        if kind in (PT_LOAD, PT_SCE_RELRO):
            out.append((vaddr, offset, filesz))
    return out


def original(elf, segs, at, length):
    for vaddr, offset, filesz in segs:
        if vaddr <= at and at - vaddr + length <= filesz and offset + at - vaddr + length <= len(elf):
            return elf[offset + at - vaddr:offset + at - vaddr + length]
    return None


def available(elf, exe, catalog=None):
    """{id: (version, [(image offset, bytes)])} of the patches that fit, and {id: reason} of the others."""
    catalog = catalog or json.loads(CATALOG.read_text(encoding='utf-8'))
    segs = segments(elf)
    fits, known = {}, set()
    for patch in catalog['patches']:
        if patch['elf'] != exe:
            continue
        known.add(patch['id'])
        writes = []
        for address, length, sha, data in patch['writes']:
            at = int(address, 16) - BASE
            here = original(elf, segs, at, length) if at >= 0 else None
            if here is None or hashlib.sha256(here).hexdigest() != sha or len(bytes.fromhex(data)) != length:
                break
            writes.append((at, bytes.fromhex(data)))
        else:
            fits.setdefault(patch['id'], []).append((patch['version'], writes))
    ok = {k: v[0] for k, v in fits.items() if len(v) == 1}
    other = {k: ('ambiguous' if k in fits else 'not for this executable') for k in known if k not in ok}
    return ok, other


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    elf_path, exe, out, wanted = Path(argv[0]), argv[1], Path(argv[2]), argv[3:]
    ok, other = available(elf_path.read_bytes(), exe)
    writes, used = [], []
    for name in wanted:
        if name in ok:
            writes += ok[name][1]
            used.append(name)
        else:
            print(f'Graphics patch {name}: {other.get(name, "unknown")}, skipped')
    blob = b'BBPATCH2' + struct.pack('<QQ', BASE, len(writes))
    for offset, data in writes:
        blob += struct.pack('<QQ', offset, len(data)) + data
    out.write_bytes(blob)
    print(f'Graphics patches: {" ".join(used) or "none"}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
