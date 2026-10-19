#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Inside the Fedora build container (packaging/armada/build.sh): installs the build dependencies,
# builds bb-probe with the FEXCore guest CPU (build.sh) and copies the shared libraries the
# container provided to out/lib, so the port runs on the host without them installed there.
set -euo pipefail
cd -- "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")/../.."

sudo=""
if [[ $(id -u) != 0 ]]; then sudo=sudo; fi
# Each dependency as alternatives ("a|b|c": capability names first, package names after), since
# names differ between Fedora releases; the first one dnf can install is used.
deps=(
    clang gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config binutils git python3 patchelf lld
    'pkgconfig(vulkan)|vulkan-loader-devel' vulkan-headers 'pkgconfig(sdl3)|SDL3-devel'
    'pkgconfig(x11)|libX11-devel' 'pkgconfig(xcb)|libxcb-devel' 'pkgconfig(wayland-client)|wayland-devel'
    'pkgconfig(libxxhash)|xxhash-devel'
    'pkgconfig(libavformat)|ffmpeg-free-devel|libavformat-free-devel'
    'pkgconfig(libavcodec)|ffmpeg-free-devel|libavcodec-free-devel'
    'pkgconfig(libavutil)|ffmpeg-free-devel|libavutil-free-devel'
    'pkgconfig(libswscale)|ffmpeg-free-devel|libswscale-free-devel'
    'pkgconfig(libswresample)|ffmpeg-free-devel|libswresample-free-devel'
    boost-devel 'cmake(fmt)|fmt-devel' 'cmake(magic_enum)|magic_enum-devel'
    'cmake(tsl-robin-map)|robin-map-devel' 'cmake(Zydis)|cmake(zydis)|zydis-devel'
    'cmake(nlohmann_json)|json-devel' 'cmake(protobuf)|protobuf-devel' protobuf-compiler
    openssl-devel 'zlib-ng-compat-devel|zlib-devel' miniupnpc-devel
    'glslang|glslang-devel' 'spirv-headers-devel|pkgconfig(SPIRV-Headers)' spirv-tools
)
optional=('miniz-devel|pkgconfig(miniz)' 'cmake(VulkanMemoryAllocator)|vulkan-memory-allocator-devel|VulkanMemoryAllocator-devel' 'spirv-cross|spirv-cross-devel')
dnf_install() { $sudo dnf install -y -q --setopt=install_weak_deps=False "$@" >/dev/null 2>&1; }
install_alternatives() { # $1: "a|b|c"; $2: required (1) or optional (0)
    local alternative
    IFS='|' read -r -a alternatives <<< "$1"
    for alternative in "${alternatives[@]}"; do
        if dnf_install "$alternative"; then echo "  ok   $alternative"; return 0; fi
    done
    if [[ $2 == 1 ]]; then echo "  FALTA $1"; missing+=("$1"); else echo "  (opcional, no disponible) $1"; fi
}
echo "== dependencies"
$sudo dnf makecache -q >/dev/null 2>&1 || true
missing=()
for dep in "${deps[@]}"; do install_alternatives "$dep" 1; done
for dep in "${optional[@]}"; do install_alternatives "$dep" 0; done
if (( ${#missing[@]} )); then
    echo "No se pudieron instalar: ${missing[*]}" >&2
    echo "Pásale este mensaje a Claude." >&2
    exit 1
fi

echo "== submodules (gpu/third_party, third_party/FEX and its own)"
git config --global --add safe.directory '*'
git submodule update --init --recursive

echo "== build (FEXCore, GPU library, runtime)"
BB_LTO=${BB_LTO:-OFF} BB_PGO=${BB_PGO:-off} bash build.sh

echo "== runtime libraries -> out/lib"
# Everything bb-probe and its libraries load from the container, except what must come from the
# host: glibc, the Vulkan loader and drivers (the host's Turnip knows this GPU), the display and
# audio servers' client libraries.
host_only='^(ld-linux|libc|libm|libpthread|libdl|librt|libresolv|libutil|libvulkan|libGL|libEGL|libGLX|libGLdispatch|libgbm|libdrm|libX11|libX11-xcb|libxcb|libXau|libXdmcp|libXext|libXrender|libXi|libXfixes|libXcursor|libXrandr|libXss|libXtst|libwayland|libdecor|libpipewire|libpulse|libasound|libdbus|libsystemd|libudev|libcap|libgcc_s|libstdc\+\+)[.-]'
rm -rf out/lib && mkdir -p out/lib
for binary in out/bb-probe out/gpu/libbbgpu.so out/fex/libbbcpu.so; do
    ldd "$binary" | awk '/=> \// { print $3 }'
done | sort -u | while read -r lib; do
    name=$(basename -- "$lib")
    if [[ ! $name =~ $host_only ]]; then cp -L -- "$lib" out/lib/; fi
done
# libstdc++/libgcc_s: the container's when newer than the host's is unknown here; ship them, the
# host's are only used if these are removed.
for name in libstdc++.so.6 libgcc_s.so.1; do
    path=$(ldconfig -p | awk -v n="$name" '$1 == n && /aarch64|AArch64/ { print $NF; exit }')
    [[ -n $path ]] && cp -L -- "$path" out/lib/
done
ls out/lib | sed 's/^/  /'
echo "== done"
