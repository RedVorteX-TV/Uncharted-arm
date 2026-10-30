"""Linux fork: scripts/render_720p.py on a synthetic executable with the collection's video setting code.
Run: python3 -m unittest discover -s tests"""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

from paths import ROOT

sys.path.insert(0, str(ROOT / 'scripts'))
import render_720p  # noqa: E402

BASE = 0x4000  # file offset of image offset 0, as in the games


def setup(reg):
    """A default setup: movl $1920/$1080 to 0x8/0xc, 0x18/0x1c, 0x24/0x28, 240/270 to 0x40/0x44, 0x38/0x3c."""
    code = b''
    for field, value in ((0x08, 1920), (0x0c, 1080), (0x18, 1920), (0x1c, 1080), (0x24, 1920), (0x28, 1080),
                         (0x40, 240), (0x44, 270), (0x38, 1920), (0x3c, 1080)):
        code += bytes([0xc7, reg, field]) + struct.pack('<I', value)
    return code


def mode_switch():
    """cmp $1, %esi; jne +0x40 (the 720p case); then the 1080p case."""
    return b'\x83\xfe\x01\x0f\x85' + struct.pack('<i', 0x40) + setup(0x47)


def decoder():
    """movl $1920, 0x58(%rsp); movl $1088, 0x5c(%rsp)"""
    return b'\xc7\x44\x24\x58\x80\x07\x00\x00\xc7\x44\x24\x5c\x40\x04\x00\x00'


def elf(*pieces):
    header = bytearray(0x40)
    struct.pack_into('<Q', header, 0x20, 0x40)  # e_phoff
    struct.pack_into('<H', header, 0x38, 1)     # e_phnum
    phdr = struct.pack('<IIQQQQQQ', 1, 5, BASE, 0, 0, 0x1000, 0x1000, 0x1000)
    body = bytearray(header + phdr)
    body += b'\0' * (BASE - len(body))
    offsets = []
    for piece in pieces:
        body += b'\x90' * 16
        offsets.append(len(body) - BASE)
        body += piece
    return bytes(body), offsets


class Render720pTests(unittest.TestCase):
    def test_patches_defaults_mode_switch_and_decoder(self):
        data, (first, second, switch, dec) = elf(setup(0x47), setup(0x40), mode_switch(), decoder())
        found = dict(render_720p.writes(data))
        # The 1080p case jumps (rel32 one byte longer, nop) to its 720p target.
        self.assertEqual(found[switch + 3], b'\xe9' + struct.pack('<i', 0x41) + b'\x90')
        for site in (first, second):
            values = sorted(struct.unpack('<I', v)[0] for o, v in found.items() if site <= o < site + 0x50)
            self.assertEqual(values, sorted([1280, 720] * 4 + [160, 180]))
        self.assertEqual(found[dec + 4], struct.pack('<I', 1280))
        self.assertEqual(found[dec + 12], struct.pack('<I', 720))
        self.assertEqual(len(found), 2 * 10 + 1 + 2)

    def test_patch_file_format(self):
        data, _ = elf(setup(0x47), setup(0x40), mode_switch(), decoder())
        with tempfile.TemporaryDirectory() as folder:
            src, out = Path(folder) / 'eboot.elf', Path(folder) / 'render-720p.bin'
            src.write_bytes(data)
            count = render_720p.patch_file(src, out)
            blob = out.read_bytes()
        self.assertEqual(blob[:8], b'BBPATCH2')
        self.assertEqual(struct.unpack_from('<QQ', blob, 8), (0x400000, count))

    def test_other_executables_are_left_alone(self):
        # No video setting block, or more than the collection's games have (1 to MAX_SITES).
        data, _ = elf(decoder())
        with self.assertRaises(ValueError):
            render_720p.writes(data)
        data, _ = elf(*[setup(0x47)] * (render_720p.MAX_SITES + 1))
        with self.assertRaises(ValueError):
            render_720p.writes(data)

    def test_one_block_at_the_end_of_the_file(self):
        # One block is accepted (since 1-3 blocks); the scan stops at the end of the file.
        data, (_decoder, site) = elf(decoder(), setup(0x47))
        values = sorted(struct.unpack('<I', v)[0] for o, v in render_720p.writes(data) if site <= o < site + 0x50)
        self.assertEqual(values, sorted([1280, 720] * 4 + [160, 180]))


if __name__ == '__main__':
    unittest.main()
