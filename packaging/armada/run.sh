#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Starts the aarch64 build (packaging/armada/build.sh) on the host: the libraries from the build
# container in out/lib, the host's Vulkan driver, then run.py (the fork's runner: the collection's
# three games, LoadExec between them, the 1280x720 mode). Logs in logs/.
#
#   BB_GAME_DIR=/path/to/CUSA02344 bash packaging/armada/run.sh      the collection menu
#   BB_EXE=u2 ... / BB_EXE=u3 ...                                   start a game directly
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
    echo "  BB_GAME_DIR=~/Games/CUSA02344 bash packaging/armada/run.sh" >&2
    exit 1
fi
if [[ ! -f $BB_GAME_DIR/eboot.bin ]]; then
    echo "FATAL: no eboot.bin in '$BB_GAME_DIR' (set BB_GAME_DIR)." >&2
    exit 1
fi
PYTHON=${PYTHON:-$(command -v python3 || true)}
if [[ -z $PYTHON ]]; then echo "FATAL: install Python 3 or set PYTHON." >&2; exit 1; fi

export LD_LIBRARY_PATH=$PORT/out/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export BB_PROBE=$PORT/out/bb-probe
export BB_DATA_DIR=${BB_DATA_DIR:-$PORT}

log_dir=${BB_LOG_DIR:-$PORT/logs}
mkdir -p "$log_dir"
log=$log_dir/run-$(date +%Y%m%d-%H%M%S).log
ln -sf -- "$(basename -- "$log")" "$log_dir/latest.log" 2>/dev/null || true
exec > >(tee -a "$log") 2>&1

echo "Uncharted (ARM, spacepepperoni's Linux fork) - $(date '+%Y-%m-%d %H:%M:%S')"
echo "  port  $PORT"
echo "  game  $BB_GAME_DIR"
echo "  log   $log"
# A quick look at the GPU the game will get (the first Vulkan device must be the Adreno).
if command -v vulkaninfo >/dev/null; then
    vulkaninfo --summary 2>/dev/null | grep -E 'deviceName|driverName|driverInfo' | head -3 | sed 's/^ */Vulkan: /'
fi
# Steam's overlay and host libraries do not belong in this process (as the AppImage's entry).
unset LD_PRELOAD
status=0
"$PYTHON" "$PORT/run.py" "$@" || status=$?
echo "Game exited with status $status"
if (( status != 0 )); then
    echo "---- possible problems (in $log) ----"
    grep -inE 'STOP:|fault|error|fail|assert|abort|segfault|core dumped|unsupported' "$log" | tail -n 30 || true
fi
exit "$status"
