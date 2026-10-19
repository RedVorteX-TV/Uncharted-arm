#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Inside the Fedora build container (packaging/armada/build.sh): installs the build dependencies,
# builds bb-probe with the FEXCore guest CPU (build.sh) and copies the shared libraries the
# container provided to out/lib, so the port runs on the host without them installed there.
set -euo pipefail
cd -- "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")/../.."

sudo=""
if [[ $(id -u) != 0 ]]; then sudo=sudo; fi
# By capability (pkgconfig()/cmake()), not package names, which differ between releases.
deps=(
    clang gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config binutils git python3 patchelf lld
    'pkgconfig(vulkan)' vulkan-headers 'pkgconfig(sdl3)' 'pkgconfig(x11)' 'pkgconfig(xcb)'
    'pkgconfig(wayland-client)' 'pkgconfig(libxxhash)'
    'pkgconfig(libavformat)' 'pkgconfig(libavcodec)' 'pkgconfig(libavutil)'
    'pkgconfig(libswscale)' 'pkgconfig(libswresample)'
    boost-devel 'cmake(fmt)' 'cmake(magic_enum)' 'cmake(tsl-robin-map)' 'cmake(zydis)'
    'cmake(nlohmann_json)' 'cmake(protobuf)' protobuf-compiler openssl-devel zlib-ng-compat-devel
    miniupnpc-devel miniz-devel glslang spirv-headers-devel spirv-tools
)
optional=('cmake(VulkanMemoryAllocator)' spirv-cross)
echo "== dependencies"
$sudo dnf install -y --setopt=install_weak_deps=False "${deps[@]}"
$sudo dnf install -y --skip-unavailable "${optional[@]}" || true

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
