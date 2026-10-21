#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the port (packaging/armada/build.sh) and packs it into dist/Uncharted-aarch64.AppImage:
# the launcher (resolution, frame rate limit, FSR 1 before the game starts) and the game, with
# FEXCore and the libraries the build container provided. Saves and settings of the AppImage live
# in ~/.local/share/uncharted-linux.
#
#   bash packaging/armada/appimage.sh
set -euo pipefail
BB_APPIMAGE=1 exec bash "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")/build.sh" "$@"
