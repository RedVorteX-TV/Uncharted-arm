#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Starts the aarch64 build (packaging/armada/build.sh) on the host: the libraries from the build
# container in out/lib, the host's Vulkan driver, then run_uncharted.sh (logs in logs/).
#
#   BB_GAME_DIR=/path/to/CUSA02320 bash packaging/armada/run.sh       Uncharted 1 / collection menu
#   BB_EXE=u2 ... / BB_EXE=u3 ...                                    start a game directly
#
# Diagnostics: logs/latest.log is the whole run; BB_FEX_LOG=1 adds FEXCore's messages.
set -uo pipefail
PORT=$(cd -- "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")/../.." && pwd)

if [[ ! -x $PORT/out/bb-probe ]]; then
    echo "Build first: bash packaging/armada/build.sh" >&2
    exit 1
fi
if [[ -z ${BB_GAME_DIR:-} ]]; then
    echo "Set BB_GAME_DIR to your game folder (the one with eboot.bin), e.g.:" >&2
    echo "  BB_GAME_DIR=~/Games/CUSA02320 bash packaging/armada/run.sh" >&2
    exit 1
fi
export LD_LIBRARY_PATH=$PORT/out/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export BB_PROBE=$PORT/out/bb-probe

# A quick look at the GPU the game will get (the first Vulkan device must be the Adreno).
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName|driverInfo' | head -3 | sed 's/^ */Vulkan: /'
fi
# Steam's overlay and host libraries do not belong in this process (as the AppImage's entry).
unset LD_PRELOAD
exec bash "$PORT/run_uncharted.sh" "$@"
