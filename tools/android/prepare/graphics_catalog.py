"""patches/graphics_patches.json -> graphics_catalog.inc (bb-prepare's copy of the catalog).

python3 tools/android/prepare/graphics_catalog.py   (from the repository root)
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
catalog = json.loads((ROOT / 'patches' / 'graphics_patches.json').read_text())
lines = ['// Generated from patches/graphics_patches.json by tools/android/prepare/graphics_catalog.py.']
for p in catalog['patches']:
    writes = ', '.join(f'{{{w[0]}, {w[1]}, "{w[2]}", "{w[3]}"}}' for w in p['writes'])
    lines.append(f'{{"{p["id"]}", "{p["elf"]}", "{p["version"]}", {{{writes}}}}},')
(Path(__file__).parent / 'graphics_catalog.inc').write_text('\n'.join(lines) + '\n')
