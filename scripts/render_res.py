#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Render resolution above (or below) the PS4's 1920x1080 for the three Uncharted games.

Ported from BilalReloaded's Uncharted: The Nathan Drake Collection PC port
(https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port, GPL-2.0-or-later,
tools/probe_uncharted.py resolution_patch), by BilalReloaded, October 2026.

The engine's display settings (constructors and the 1080p/720p mode switch, all within ~0x700
bytes) store the size in several fields: display and render size (+0x08/+0x18/+0x24/+0x38),
aspect, tile grid (+0x40 = width/8, +0x44 = height/4, as the engine's own SetResolution computes
them) and a larger size at +0x30/+0x34 (2560x1216 at 1080p), scaled with the rest. The render
targets come from a fixed "Render Target Heap" inside the engine's memory table: it grows with
the pixel count, and the direct memory the runtime reports grows by the same amount (BB_DMEM_MB).
Everything is found by byte pattern, so 1.00 and 1.02 of every region are covered.

usage: render_res.py <boot-linked.bin> --out <patched image> --exe <u1|u2|u3> --size WxH
Writes the patched image and <out>.json: {"render": "WxH", "extra_mib": N}. Drake's Fortune
keeps its render targets in the GPU area, so above ~1620p it renders at the largest 16:9 size
that fits there (the json says which).
"""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import sys


class ResolutionTooLarge(Exception):
    pass


# Drake's Fortune above what its GPU area holds: these 16:9 sizes in turn (width/8, height/4 whole).
U1_FALLBACK_SIZES = [(3200, 1800), (2880, 1620), (2560, 1440), (1920, 1080)]


def pattern_regex(text):
    text = text.replace(" ", "")
    return re.compile(b"".join(b"." if text[i:i + 2] == "??" else re.escape(bytes.fromhex(text[i:i + 2]))
                               for i in range(0, len(text), 2)), re.S)


def resolution_patch(data, width, height, move=True):
    # mov [rdi/rax+8],1920; mov [rdi/rax+0xc],1080 (1.00 starts with rdi, 1.02 with rax)
    anchor = pattern_regex("c7 ?? 08 80070000 c7 ?? 0c 38040000")
    match = anchor.search(data)
    if not match:
        raise SystemExit("Resolution patch: display settings code not found")
    first = match.start()
    start, end = first - 0x40, first + 0x800
    region = bytearray(data[start:end])
    stores = [
        (0x08, 1920, width), (0x18, 1920, width), (0x24, 1920, width), (0x38, 1920, width),
        (0x0c, 1080, height), (0x1c, 1080, height), (0x28, 1080, height), (0x3c, 1080, height),
        (0x40, 240, width // 8), (0x44, 270, height // 4),
        (0x30, 2560, 2560 * width // 1920), (0x34, 1216, 1216 * height // 1080),
    ]
    # Diagnostics: BB_RES_KEEP=<hex field offsets> leaves those fields at 1080p.
    keep = {int(v, 16) for v in os.environ.get("BB_RES_KEEP", "").replace(",", " ").split()}
    stores = [store for store in stores if store[0] not in keep]
    changed = 0
    # movl $imm32, disp8(%reg) with any base register but rsp (0x44 needs a SIB byte); r8-r15 add a
    # REX prefix before the C7. CUSA02320 uses %rdi (1.00) and %rax (1.02); other dumps (CUSA02344)
    # other registers (as scripts/render_720p.py found).
    for base in (0x47, 0x40, 0x41, 0x42, 0x43, 0x45, 0x46):
        for disp, old, new in stores:
            old_bytes = bytes([0xc7, base, disp]) + old.to_bytes(4, "little")
            new_bytes = bytes([0xc7, base, disp]) + new.to_bytes(4, "little")
            changed += region.count(old_bytes)
            region[:] = region.replace(old_bytes, new_bytes)
    packed_old = bytes.fromhex("48b8") + (1920).to_bytes(4, "little") + (1080).to_bytes(4, "little")
    packed_new = bytes.fromhex("48b8") + width.to_bytes(4, "little") + height.to_bytes(4, "little")
    changed += region.count(packed_old)
    region[:] = region.replace(packed_old, packed_new)
    data[start:end] = region
    # The render targets come from a fixed "Render Target Heap" (497 MiB at 1080p) inside the
    # 2988 MiB GPU (garlic) block of the engine's memory table; both grow with the pixel count
    # (+10%), and the direct memory reported to the game by the same amount (BB_DMEM_MB).
    mib = 1 << 20
    factor = max(1.0, width * height / (1920 * 1080))

    def table_entry(entry_id, parent, align, min_size):
        """The memory table entry (id, parent, size, alignment, 0) by structure; sizes differ per game."""
        found = []
        key = struct.pack("<II", entry_id, parent)
        at = data.find(key)
        while at >= 0:
            if at + 32 <= len(data):
                size, entry_align, pad = struct.unpack_from("<QQQ", data, at + 8)
                if entry_align == align and pad == 0 and size >= min_size:
                    found.append((at, size))
            at = data.find(key, at + 1)
        if len(found) != 1:
            raise SystemExit(f"Resolution patch: memory table entry {entry_id:#x} found {len(found)} times")
        return found[0]

    # Memory areas (a table entry's second field is the context it lies in): entry 2 = CPU
    # ("onion", context 5) and entry 3 = GPU ("garlic", context 6). An area holds at most 4096 MiB
    # (MemoryArea keeps one physical page id per MiB in a 4096-entry array), so above ~1620p the
    # Render Target Heap moves to the CPU area; on PC both areas are the same memory. Drake's
    # Fortune can't address its heaps there (its offset pointers cover [0, 2 GiB)), so it stops
    # where the GPU area is full (move=False).
    onion_at, onion_old = table_entry(2, 2, 0x100000, 0x40000000)
    garlic_at, garlic_old = table_entry(3, 2, 0x100000, 0x80000000)
    heap_at, heap_old = table_entry(0x85, 6, 0x80, 0x10000000)       # Render Target Heap
    rt_at, rt_old = table_entry(0x84, 6, 0x80, 0x4000000)            # Render Target
    step = 64 * mib  # the engine's direct memory blocks are large-page multiples
    area_limit, headroom = 4096 * mib, 128 * mib

    def layout(margin):
        def grown(old):
            return old + (-(-int(old * (factor * margin - 1)) // step) * step if factor > 1 else 0)

        heap_new, rt_new = grown(heap_old), grown(rt_old)
        onion_new, garlic_new, heap_context = onion_old, garlic_old + (heap_new - heap_old) + (rt_new - rt_old), 6
        if garlic_new >= area_limit:
            if not move:
                raise ResolutionTooLarge(f"{width}x{height}: the render targets don't fit the GPU area")
            # Area sizes must be whole MiB (MemoryArea::Initialize asserts): the moved heap is
            # rounded up, and the GPU area's rounding goes to the other render target heap.
            heap_new = -(-heap_new // step) * step
            onion_new = onion_old + heap_new
            rest = garlic_old - heap_old - rt_old
            garlic_new = -(-(rest + rt_new) // step) * step
            rt_new = garlic_new - rest
            heap_context = 5
        # Uncharted 3 1.02 maps its CPU area 100 MiB larger than the table says: keep 128 MiB
        # below the limit.
        if onion_new + headroom >= area_limit or garlic_new >= area_limit:
            raise ResolutionTooLarge(f"{width}x{height} needs more memory than the game's areas hold")
        return heap_new, rt_new, onion_new, garlic_new, heap_context

    # The render target heaps grow with the pixel count plus 10%, less when that doesn't fit.
    for margin in (1.1, 1.05, 1.0):
        try:
            heap_new, rt_new, onion_new, garlic_new, heap_context = layout(margin)
            break
        except ResolutionTooLarge:
            if margin == 1.0:
                raise
    extra = (onion_new - onion_old) + (garlic_new - garlic_old)
    struct.pack_into("<IIQ", data, heap_at, 0x85, heap_context, heap_new)
    struct.pack_into("<Q", data, rt_at + 8, rt_new)
    struct.pack_into("<Q", data, onion_at + 8, onion_new)
    struct.pack_into("<Q", data, garlic_at + 8, garlic_new)
    # The memory table check (ValidateContext: lea rsi,[rbp-0x20 or -0x30]; movabs rdx,<total>)
    # expects the direct memory blocks (context 2) to add up to 4608 MiB.
    total_check = pattern_regex("488d75 ?? 48ba 0000002001000000")
    found = [m.start() for m in total_check.finditer(data)]
    if len(found) != 1:
        raise SystemExit(f"Resolution patch: memory total check found {len(found)} times")
    data[found[0] + 6:found[0] + 14] = (0x120000000 + extra).to_bytes(8, "little")
    print(f"Patched: render resolution {width}x{height} ({changed} stores; render target heaps "
          f"{heap_new // mib} + {rt_new // mib} MiB, the first in the {'CPU' if heap_context == 5 else 'GPU'} area)")
    return extra // mib


def parse_size(text):
    try:
        width, height = (int(value) for value in text.lower().split("x"))
    except ValueError:
        raise SystemExit(f"Render resolution: '{text}' is not WIDTHxHEIGHT")
    if not (640 <= width <= 7680 and 360 <= height <= 4320) or width % 8 or height % 4:
        raise SystemExit(f"Render resolution: {width}x{height} is not supported "
                         "(width a multiple of 8, height of 4, 640x360 .. 7680x4320)")
    return width, height


def patch_image(data, game, width, height):
    """(patched image, (render width, render height), extra MiB) for one game (u1/u2/u3)."""
    move = game != "u1"
    candidates = [(width, height)] + [size for size in U1_FALLBACK_SIZES if size[0] * size[1] < width * height]
    if abs(width / height - 16 / 9) > 0.01:
        # Other shapes: smaller sizes of the same shape (width/16 and height/4 whole: Uncharted 3
        # asserts widths in steps of 16 in its post processing).
        candidates = [(width, height)] + [
            (int(width * scale) // 16 * 16, int(height * scale) // 4 * 4)
            for scale in (0.9, 0.8, 0.75, 2 / 3, 0.6, 0.5)]
    for candidate in candidates:
        patched = bytearray(data)
        try:
            extra_mib = resolution_patch(patched, *candidate, move=move)
        except ResolutionTooLarge as error:
            print(f"Render resolution {error}")
            continue
        if candidate != (width, height):
            print(f"Render resolution: {game.upper()} renders at {candidate[0]}x{candidate[1]} "
                  f"instead of {width}x{height} (its memory layout)")
        return patched, candidate, extra_mib
    raise SystemExit(f"Render resolution: no size up to {width}x{height} fits {game.upper()}'s memory")


# movl $1920, d(%rsp); movl $1088, d+4(%rsp): the movie decoder's maximum frame size, as
# scripts/render_720p.py patches it. Below 1080p (the ARM port's 540p) Videodec2 then fits the
# 1080p movies to the game's own movie textures, and BB_MOVIE_SIZE's scratch frames (flexible
# memory, which is full on the Thor) are not needed.
MOVIE_DECODER = re.compile(rb"\xc7\x44\x24(.)\x80\x07\x00\x00\xc7\x44\x24(.)\x40\x04\x00\x00", re.S)


def patch_movie_decoder(data, width, height):
    """The movie decoder's maximum frame size set to width x height; False (unchanged) unless
    there is exactly one decoder setup."""
    sites = [m for m in MOVIE_DECODER.finditer(data) if m.group(2)[0] == m.group(1)[0] + 4]
    if len(sites) != 1:
        print(f"Render resolution: {len(sites)} movie decoder setups (one expected): movies through "
              "BB_MOVIE_SIZE")
        return False
    at = sites[0].start()
    data[at + 4:at + 8] = width.to_bytes(4, "little")
    data[at + 12:at + 16] = height.to_bytes(4, "little")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--exe", required=True, help="u1, u2 or u3")
    parser.add_argument("--size", required=True, help="WIDTHxHEIGHT")
    parser.add_argument("--movie-decoder", action="store_true",
                        help="also the movie decoder's maximum frame size (sizes below 1080p)")
    args = parser.parse_args()
    game = args.exe.lower()
    if game not in ("u1", "u2", "u3"):
        raise SystemExit(f"Render resolution: unknown game {args.exe}")
    width, height = parse_size(args.size)
    data = bytearray(args.image.read_bytes())
    patched, render, extra_mib = patch_image(data, game, width, height)
    movie_decoder = args.movie_decoder and patch_movie_decoder(patched, *render)
    temporary = args.out.with_name(args.out.name + ".tmp")
    temporary.write_bytes(patched)
    os.replace(temporary, args.out)
    meta = {"render": f"{render[0]}x{render[1]}", "extra_mib": extra_mib, "requested": f"{width}x{height}",
            "movie_decoder": bool(movie_decoder)}
    args.out.with_suffix(".json").write_text(json.dumps(meta), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
