"""Linux fork: a BBPATCH2 file (src/probe.c --patches) that makes an Uncharted game render at 1280x720.

The collection's engine keeps its video settings in one structure: two default setups and a mode
switch whose 1080p case sets every size field (scene, UI, viewport) to 1920x1080; the PS4's 720p
case already exists. The defaults get the 720p values (1280x720, and 160x180 for 0x40/0x44) and
the 1080p case jumps to the 720p case, so the game allocates and renders everything at 720p,
movies and menus included (Steam Deck mode). The sites are found by
their instruction bytes, the same in U1, U2 and U3.

    python3 render_720p.py <prepared eboot.elf> <out.bin>
"""
import os
import re
import struct
import sys

WIDTH, HEIGHT = 1920, 1080
NEW_WIDTH, NEW_HEIGHT = 1280, 720
# movl $imm32, disp8(%reg): an optional REX.B prefix (r8-r15), opcode C7, ModRM mod=01 reg=0 with
# any base register except rsp (0x44, which needs a SIB byte). CUSA02320 builds the structure through
# %rdi and %rax; other dumps (CUSA02344) use other registers.
MOVL = rb'\x41?\xc7[\x40-\x43\x45-\x47]'
MODRM = frozenset(range(0x40, 0x48)) - {0x44}
# movl $1920, 0x8(%reg); movl $1080, 0xc(%reg): the first two size fields.
SITE = re.compile(MOVL + rb'\x08\x80\x07\x00\x00' + MOVL + rb'\x0c\x38\x04\x00\x00')
WINDOW = 0x90  # the size fields of one setup follow within this many bytes
# movl $1920, d(%rsp); movl $1088, d+4(%rsp): the movie decoder's maximum frame size (Videodec2
# configuration). The movie shader maps its 720p picture to that size, so it becomes 1280x720
# and the 1080p movies arrive scaled to it (Videodec2 scales frames down to the maximum).
DECODER = re.compile(rb'\xc7\x44\x24(.)\x80\x07\x00\x00\xc7\x44\x24(.)\x40\x04\x00\x00', re.S)
# CUSA02320 has two default setups and the 1080p mode; CUSA02344 has been reported with one
# block (Linux fork PR #23). Every block found is patched.
MAX_SITES = 3


def image_base(elf):
    """File offset minus image offset of the first program header (the image maps vaddr 0 at 0)."""
    phoff = struct.unpack_from('<Q', elf, 0x20)[0]
    _, _, offset, vaddr, *_ = struct.unpack_from('<IIQQQQQQ', elf, phoff)
    return offset - vaddr


def field_of(elf, at):
    """The structure offset an immediate is stored to (movl disp8, or 0x38/0x3c for movabs)."""
    if elf[at - 3] == 0xc7 and elf[at - 2] in MODRM:
        return elf[at - 1]
    return 0x38 if elf[at - 2:at] == b'\x48\xb8' else 0x3c


def writes(elf, fields=None):
    base = image_base(elf)
    sites = [m.start() for m in SITE.finditer(elf)]
    if not 1 <= len(sites) <= MAX_SITES:
        raise ValueError(f'expected 1 to {MAX_SITES} video setting sites, found {len(sites)}')
    out = []
    for index, site in enumerate(sites):
        # A setup's fields end before the next one (and its cmp/jne) begins.
        # The last 4-byte value read starts 4 bytes before the end of the file.
        end = min(site + WINDOW, sites[index + 1] - 9 if index + 1 < len(sites) else len(elf), len(elf) - 3)
        # cmp $1, %esi; jne <next mode>: the mode switch's 1080p case. It jumps to the game's own
        # 720p case instead (sizes, the 160x180 fields, the 1280.0/720.0 it passes on).
        # (cmp $1 with any 32-bit register: 83 F8+r 01.)
        if (elf[site - 9] == 0x83 and 0xf8 <= elf[site - 8] <= 0xff and elf[site - 7] == 0x01 and
                elf[site - 6:site - 4] == b'\x0f\x85'):
            if fields is None:
                rel = struct.unpack_from('<i', elf, site - 4)[0] + 1  # jmp rel32 is one byte shorter
                out.append((site - 6 - base, b'\xe9' + struct.pack('<i', rel) + b'\x90'))
                continue
        for at in range(site, end):
            value = struct.unpack_from('<I', elf, at)[0]
            new = {WIDTH: NEW_WIDTH, HEIGHT: NEW_HEIGHT, WIDTH // 8: NEW_WIDTH // 8,
                   HEIGHT // 4: NEW_HEIGHT // 4}.get(value)
            if new is None:
                continue
            # movl $imm32, disp8(%reg), or the two halves of movabs $imm64, %rax
            movl = elf[at - 3] == 0xc7 and elf[at - 2] in MODRM
            movabs = elf[at - 2:at] == b'\x48\xb8' or elf[at - 6:at - 4] == b'\x48\xb8'
            # 1920/8 and 1080/4 only in fields 0x40/0x44 (the 1080p mode's 240x270)
            if value in (WIDTH // 8, HEIGHT // 4) and not (movl and elf[at - 1] in (0x40, 0x44)):
                continue
            if (movl or movabs) and (fields is None or field_of(elf, at) in fields):
                out.append((at - base, struct.pack('<I', new)))
    decoders = [m for m in DECODER.finditer(elf) if m.group(2)[0] == m.group(1)[0] + 4]
    if len(decoders) != 1:
        raise ValueError(f'expected one movie decoder setup, found {len(decoders)}')
    if fields is None:
        at = decoders[0].start()
        out.append((at + 4 - base, struct.pack('<I', NEW_WIDTH)))
        out.append((at + 12 - base, struct.pack('<I', NEW_HEIGHT)))
    return out


def patch_file(elf_path, out_path, fields=None):
    elf = open(elf_path, 'rb').read()
    found = writes(elf, fields)
    blob = b'BBPATCH2' + struct.pack('<QQ', 0x400000, len(found))
    for offset, data in found:
        blob += struct.pack('<QQ', offset, len(data)) + data
    with open(out_path, 'wb') as f:
        f.write(blob)
    return len(found)


if __name__ == '__main__':
    try:
        # BB_RENDER_720P_FIELDS=8,18,... (hex structure offsets, testing): only these fields.
        only = os.environ.get('BB_RENDER_720P_FIELDS')
        fields = {int(f, 16) for f in only.split(',')} if only else None
        print(f'Render 720p: {patch_file(sys.argv[1], sys.argv[2], fields)} size values patched')
    except (OSError, ValueError, struct.error) as error:
        print(f'Render 720p: {error}')
        sys.exit(1)
