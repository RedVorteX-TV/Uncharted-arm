#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the aarch64 port on an ARM handheld running Armada (Fedora bootc, read-only /usr) or any
# other aarch64 Fedora: the build runs in a Fedora container of the host's release (podman or
# toolbox), so nothing is installed on the system. The result stays in out/ of this checkout,
# with the libraries the container provided copied to out/lib (run with packaging/armada/run.sh).
#
#   bash packaging/armada/build.sh          build (first run: about 30-60 min, mostly FEX)
#   BB_CLEAN=1 bash packaging/armada/build.sh   start over (removes out/)
set -euo pipefail
PORT=$(cd -- "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")/../.." && pwd)
cd -- "$PORT"

if [[ $(uname -m) != aarch64 ]]; then
    echo "This script builds the aarch64 port; on x86-64 use build.sh directly." >&2
    exit 1
fi
if [[ ${BB_CLEAN:-0} == 1 ]]; then rm -rf out; fi
mkdir -p out

# Same Fedora release as the host so the copied libraries match its glibc.
. /etc/os-release
release=${BB_FEDORA_RELEASE:-${VERSION_ID:-44}}
image=registry.fedoraproject.org/fedora:$release
echo "Host: ${PRETTY_NAME:-unknown}; build container: $image"

inner=packaging/armada/container_build.sh
if command -v podman >/dev/null; then
    # Rootless podman: root in the container is this user on the host, so out/ stays ours.
    # A named volume keeps the dnf cache between builds.
    podman run --rm --net=host --security-opt label=disable \
        -e BB_LTO="${BB_LTO:-OFF}" -e BB_PGO="${BB_PGO:-off}" -e BB_APPIMAGE="${BB_APPIMAGE:-0}" \
        -v uncharted-dnf-cache:/var/cache/libdnf5 \
        -v "$PORT:$PORT" -w "$PORT" "$image" bash "$inner"
elif command -v toolbox >/dev/null; then
    toolbox create -y -r "$release" -c uncharted-build 2>/dev/null || true
    toolbox run -c uncharted-build env BB_APPIMAGE="${BB_APPIMAGE:-0}" bash "$inner"
elif command -v distrobox >/dev/null; then
    distrobox create -Y -i "$image" -n uncharted-build 2>/dev/null || true
    distrobox enter uncharted-build -- env BB_APPIMAGE="${BB_APPIMAGE:-0}" bash "$inner"
else
    echo "Need podman, toolbox or distrobox to build without touching the system." >&2
    exit 1
fi
echo
echo "Built: $PORT/out/bb-probe (+ out/lib, out/fex/libbbcpu.so, out/gpu/libbbgpu.so)"
echo "Run:   BB_GAME_DIR=/path/to/CUSA02320 bash packaging/armada/run.sh"
