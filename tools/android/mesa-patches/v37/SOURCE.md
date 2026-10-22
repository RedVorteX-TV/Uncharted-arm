# V37 recipe: source and license

These files are StevenMXZ's "V37" Adreno 8xx recipe for Mesa's Turnip driver, copied unchanged
(byte for byte; compare with `cmp`) from his repository:

- Repository: https://github.com/StevenMXZ/Adreno-Tools-Drivers
- Commit: `b3b32ad` ("Update Turnip Android build and patch sets")
- Recipe: the `a8xx-patchs2` variant of his `build_turnip.sh` (`apply_patchs2_a8xx()`)
- License: GPL-3.0, his `LICENSE` copied as `LICENSE.GPL-3.0` here. These files stay GPL-3.0; a
  Turnip build that contains them is distributed with them (the release's `patches-*.tar.gz`)
  and with the license text.

| Here | In his repository (`patches/patchs2/`) |
|---|---|
| `common/kgsl-syncobj-merge-ts-fd.patch` | same path (not applied: Mesa main fixed it, see below) |
| `common/a8xx-cube-coord-sanitize.patch` | same path |
| `common/a8xx-bindless-invalidate.patch` | same path |
| `common/a8xx-kgsl-ib-vbo-alias.patch` | same path |
| `common/kgsl-zero-timeout-poll.patch` | same path (not applied: Mesa main has it, see below) |
| `common/apply_common.sh` | same path (not run, see below) |
| `a8xx_gen8.patch` | same path |
| `a8xx_shared_mem.py` | same path |
| `a840v2.py` | same path |

## Apply order of his `a8xx-patchs2` variant

1. `common/apply_common.sh <mesa>`: `patch -p1 -N --fuzz=3` of `kgsl-syncobj-merge-ts-fd`,
   `a8xx-cube-coord-sanitize`, `a8xx-bindless-invalidate`, `a8xx-kgsl-ib-vbo-alias`,
   `kgsl-zero-timeout-poll`, then asserts that each fix is in the tree.
2. `patch -p1 -N --fuzz=4 < a8xx_gen8.patch`
3. `python3 a8xx_shared_mem.py` (every `cs_shared_mem_size` 32 KiB to 64 KiB)
4. `python3 a840v2.py` (Adreno 840v2, chip id `0x44050A21`)

His `REVERT_COMMIT` (a70d2af5, D32S8 early-Z workaround) is only used by his A7xx variants,
not by this one. His Android/NDK steps (cross file, gralloc and `vk_android.c` seds,
`-Dplatforms=android`) do not apply to this app's Linux (glibc, X11 WSI) build and are not used.

## What `build_turnip.sh gen8v37` does with it

Built on upstream Mesa main `1dee441c7ff6bbccc4da5221ca7b146339f317dc` (his recipe targets an
older main, so several things needed handling; nothing is skipped silently):

- `kgsl-syncobj-merge-ts-fd.patch` does not apply: Mesa main rewrote `kgsl_syncobj_merge` and
  converts the aggregate timestamp to an fd (`kgsl_syncobj_ts_to_fd(&ret)`) before merging, so the
  bug it fixes is gone. Not applied; the build asserts the fix is in the tree.
- `kgsl-zero-timeout-poll.patch` does not apply: Mesa main's `wait_timestamp_safe` polls
  (`kgsl_poll_timestamp`) on a zero timeout instead of calling the wait ioctl. Not applied;
  asserted. (Our old `gen8/0001-0004` KGSL wait fixes are also in Mesa main now: not applied.)
- `apply_common.sh` is therefore not run; `build_turnip.sh` applies the three remaining patches with
  the same `patch` options and runs the equivalent asserts.
- `a8xx_gen8.patch`: one hunk is rejected (the `A8XX HELPER FUNCTIONS` block appended to
  `src/freedreno/ir3/ir3_compiler.h`; Mesa main changed the end of that file). `build_turnip.sh`
  accepts exactly that one rejected hunk and fails on any other. `ours/0001` is that hunk, same
  text, placed after `ir3_shader_bisect_disasm_select()`.
- Mesa main removed the `magic_regs` argument of `A6xxGPUInfo`; the A825 entry of
  `a8xx_gen8.patch` still passes `magic_regs = dict()` (empty), which breaks the generation of the
  device table. `ours/0002` drops that line.
- Our `0001` (A840 patch-id wildcard) no longer applies after `a840v2.py`; `ours/0003` is the same
  entry in the new context. The other patches of `../` (0005 to 0009) apply unchanged.

Order in the build, as recorded in `BUILD-INFO.json`: the three `common/` patches, `a8xx_gen8.patch`,
`ours/0001`, `ours/0002`, `a8xx_shared_mem.py`, `a840v2.py`, `ours/0003`, then `../0005` to `../0009`.
Python scripts run as `python3 -I` from the Mesa tree.
