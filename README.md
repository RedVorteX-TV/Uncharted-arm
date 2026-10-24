# Uncharted: The Nathan Drake Collection for ARM (Linux and Android)

**[Leer en español](README.es.md)** · **[RedVorteX-TV on YouTube](https://youtube.com/@redvortex-tv)**

Uncharted 1, 2 and 3 from the PS4 collection on ARM handhelds and phones with a Snapdragon GPU: Linux
(AYN Odin 3 with Armada, an AppImage) and Android (an APK). The games' x86-64 code runs through
FEXCore and their graphics go to Vulkan (Mesa Turnip on Adreno), on the same native-port base as the
Linux and Windows builds of the collection.

**Work in progress.** All three games start and play, with known problems (below). This is an
unofficial fan project, not affiliated with Sony Interactive Entertainment or Naughty Dog.

## Status

| | Linux (Odin 3, Snapdragon 8 Elite) | Android (AYN Thor, Snapdragon 8 Gen 2) |
|---|---|---|
| Uncharted 1 | Perfectly playable | Playable |
| Uncharted 2 | Perfectly playable | In-game: runs, but drops below 20 FPS at times |
| Uncharted 3 | Perfectly playable | In-game: runs, but drops below 20 FPS at times |

- Perfectly playable: smooth and steady from start to end. Playable: can be played through at a
  steady enough frame rate. In-game: runs and can be played, but the frame rate is not good enough
  yet to call it playable.
- On the Odin 3 the limit is the CPU's translation of every draw to Vulkan (about 2,000 per frame):
  Uncharted 2 runs at about 33 FPS in eco mode at 720p. On the Thor the limit is the GPU: Uncharted 1
  runs at 26-30 FPS, Uncharted 2 and 3 at 20-30 with drops below 20 in demanding areas. A lower
  internal resolution (540p) for the Thor is the next step.
- Known problems: characters sometimes clip through scenery in Uncharted 2; a crash at the end of
  one Uncharted 2 cinematic was seen once.

## What you need

- **Your own decrypted dump** of the collection (CUSA02320, CUSA02343, CUSA02344, ...): the folder
  with `eboot.bin`, `big2-ps4_Shipping.elf`, `big3-ps4_Shipping.elf`, `sce_sys`, `u1data`,
  `u2data` and `u3data`. **No game files are included or provided.**
- **Linux:** an aarch64 system with a Turnip Vulkan driver (Fedora-based Armada on the AYN Odin 3
  is the tested one) and 8 GB of RAM or more.
- **Android:** Android 9 or newer, an Adreno GPU (Snapdragon 8 Gen 2 or newer recommended), 8 GB of
  RAM or more. The GPU driver (Turnip) is bundled with the app.

## Install

**Linux (AppImage):** download `Uncharted-aarch64.AppImage` from the
[releases](https://github.com/RedVorteX-TV/Uncharted-arm/releases), make it executable and open
it. Choose the game folder in the launcher. The launcher updates itself from this repository's
releases.

**Android (APK):** install the APK from the
[releases](https://github.com/RedVorteX-TV/Uncharted-arm/releases), open it, allow storage access,
choose the game folder and press Play. With "Check for updates" on (Project section) the launcher
tells you when a newer version is out.

## Frame generation with Lossless Scaling (LSFG)

The port can use Lossless Scaling's frame generation (LSFG) at 2×, 3× or 4×. It needs
**your own `Lossless.dll`** from a copy of [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/)
that you bought. The DLL is not included and will not be provided. Choose the file in the launcher
(Linux: Settings > Lossless Scaling (LSFG); Android: Graphics > Lossless.dll, which copies it into
the app), then pick LSFG as the frame generation, there or in the in-game menu (L3+R3). It works
from the finished frames only, so it adds a little latency and can show artifacts around
fast-moving edges and the UI, more so the lower the game's own frame rate. On the Thor, use 2×.

## Game display rate (60 / 120 Hz)

Settings (Linux) or Graphics (Android) > Game display rate. At 120 Hz a frame that takes a little
over 33 ms is shown at once instead of being held until 50 ms: steadier frame times on 120 Hz
screens such as the Odin 3's and the Thor's. Keep 60 Hz on a 60 Hz screen.

## Build

- **Linux ARM** (this branch, `main`): on the device, `bash packaging/armada/build.sh` builds inside
  a Fedora container (podman or toolbox) without installing anything on the system;
  `BB_GAME_DIR=/path/to/dump bash packaging/armada/run.sh` starts it, and
  `bash packaging/armada/appimage.sh` makes the AppImage.
- **Android** (branch `android`): `tools/android/build_bionic_deps.sh`, `build_bionic.sh`,
  `build_prepare.sh` and `build_frontend.sh` (NDK, no Gradle); see the scripts' headers.

Logs are in `logs/latest.log` (Linux) and `Download/Uncharted` (Android). `BB_FRAME_STATS=1` adds
frame time, GPU waits and memory every few seconds.

## Credits

ARM and Android port: **[RedVorteX-TV](https://youtube.com/@redvortex-tv)**.

Built on the work of:

- [spacepepperoni/uncharted_Linux](https://github.com/spacepepperoni/uncharted_Linux): the Linux
  and Steam Deck fork this port starts from (launcher, 720p mode, Uncharted 2 and 3 fixes), with ngin
  and its contributors.
- [Supermedo/uncharted_pc](https://github.com/Supermedo/uncharted_pc): the native port of the
  collection.
- [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) (bbport): the
  native PS4 port base it runs on.
- [MaSieS4Fun/ARM_bloodborne_pc](https://github.com/MaSieS4Fun/ARM_bloodborne_pc): the FEXCore
  layer that runs the games' x86-64 code on ARM.
- [ducvd89/Bloodborne-Android](https://github.com/ducvd89/Bloodborne-Android): the Android
  runtime, and the FSR 3.1 fixes for Adreno and Turnip.
- [Pepin208/uncharted_pc_linux](https://github.com/Pepin208/uncharted_pc_linux): the first ARM
  attempt this port started from.
- Mevlock's uncharted_pc-dev: the patch that skips the collection menu.
- [BilalReloaded's Uncharted PC port](https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port)
  (GPL-2.0-or-later), as Tutuzaooo brought it to the Linux fork: the render resolution and the
  upscaling presets (scripts/render_res.py, cutscene movie scaling in gpu/shim/bbport_movie.cpp).
- [Eden](https://git.eden-emu.dev/eden-emu/eden) (PR #4263) and
  [ARMSX2](https://github.com/ARMSX2/ARMSX2): the LSFG frame generation passes, from
  [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk)'s
  ([gpu/shadps4/video_core/renderer_vulkan/lsfg](gpu/shadps4/video_core/renderer_vulkan/lsfg/README.md)).

And on [FEX-Emu](https://github.com/FEX-Emu/FEX) (FEXCore), [Mesa](https://mesa3d.org) (Turnip),
[shadPS4](https://github.com/shadps4-emu/shadPS4), [FFmpeg](https://ffmpeg.org) and the projects
listed in the [Linux fork's README](docs/original-readme/README.linux-fork.md). Every commit in
this repository keeps its author.

## License

The source is under the GNU GPL v2 or later ([LICENSE](LICENSE)), like the projects it is built on.
The LSFG frame generation files are GPL-3.0-or-later, so the binaries, which include them, are
distributed under the GNU GPL v3 or later as a whole ([COPYING.GPL-3](COPYING.GPL-3)).
Third-party components keep their own licenses (their texts ship with the AppImage and the APK).
