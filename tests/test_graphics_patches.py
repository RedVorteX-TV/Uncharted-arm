import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts'))
import graphics_patches  # noqa: E402


def elf_with(data, at):
    """One PT_LOAD at file offset 0x1000 for vaddr 0..0x2000, `data` at vaddr `at`."""
    elf = bytearray(0x3000)
    elf[:4] = b'\x7fELF'
    struct.pack_into('<Q', elf, 0x20, 0x40)
    struct.pack_into('<H', elf, 0x38, 1)
    struct.pack_into('<IIQQQQQQ', elf, 0x40, 1, 5, 0x1000, 0, 0, 0x2000, 0x2000, 0x1000)
    elf[0x1000 + at:0x1000 + at + len(data)] = data
    return bytes(elf)


class GraphicsPatchesTest(unittest.TestCase):
    def test_catalog(self):
        catalog = json.loads(graphics_patches.CATALOG.read_text())
        ids = {p['id'] for p in catalog['patches']}
        self.assertIn('no_ssao', ids)
        self.assertFalse(any(p['name'].startswith('Resolution') for p in catalog['patches']))
        for p in catalog['patches']:
            for address, length, sha, data in p['writes']:
                self.assertEqual(len(bytes.fromhex(data)), length)
                self.assertEqual(len(sha), 64)
                self.assertGreaterEqual(int(address, 16), graphics_patches.BASE)

    def test_matching_version_only(self):
        orig = bytes(range(16))
        sha = hashlib.sha256(orig).hexdigest()
        catalog = {'patches': [
            {'id': 'a', 'elf': 'eboot.bin', 'version': '01.02', 'writes': [['0x400123', 16, sha, 'aa' * 16]]},
            {'id': 'a', 'elf': 'eboot.bin', 'version': '01.00', 'writes': [['0x400200', 16, '0' * 64, 'bb' * 16]]},
            {'id': 'b', 'elf': 'eboot.bin', 'version': '01.02', 'writes': [['0x400123', 16, '0' * 64, 'cc' * 16]]},
            {'id': 'c', 'elf': 'big2-ps4_Shipping.elf', 'version': '01.02', 'writes': [['0x400123', 16, sha, 'dd' * 16]]},
            {'id': 'd', 'elf': 'eboot.bin', 'version': '01.02', 'writes': [['0x401ff8', 16, sha, 'ee' * 16]]},
        ]}
        ok, other = graphics_patches.available(elf_with(orig, 0x123), 'eboot.bin', catalog)
        self.assertEqual(ok, {'a': ('01.02', [(0x123, b'\xaa' * 16)])})
        self.assertEqual(other, {'b': 'not for this executable', 'd': 'not for this executable'})

    def test_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            elf = Path(tmp) / 'eboot.elf'
            elf.write_bytes(elf_with(b'', 0))
            out = Path(tmp) / 'gfx.bin'
            self.assertEqual(graphics_patches.main([str(elf), 'eboot.bin', str(out), 'no_ssao']), 0)
            self.assertEqual(out.read_bytes(), b'BBPATCH2' + struct.pack('<QQ', 0x400000, 0))


if __name__ == '__main__':
    unittest.main()
