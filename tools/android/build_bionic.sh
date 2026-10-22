#!/usr/bin/env bash
# Bionic (Android NDK) build of the native game process, the Android app's runtime (adapted from
# ducvd89's Bloodborne-Android for the Uncharted ARM port): libbbcpu.so (FEXCore), libbbgpu.so
# (GPU/HLE), bb-probe (runtime executable), kgsl-chip-id and bb-prepare (game preparation).
# Output: out/bionic/{bin,lib}.
# Prerequisite: tools/android/build_bionic_deps.sh (NDK + third-party libraries).
# Header-only packages (Boost, tsl-robin-map) come from BB_ARM64_HEADERS (an include/ tree).
# STL: c++_shared for all components (one libc++_shared.so for libbbcpu/libbbgpu/bb-probe).
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=out/bionic
root=$PWD/.local-deps/android
ndk=${BB_NDK:-$root/sdk/ndk/29.0.14206865}
api=${BB_ANDROID_API:-30}
prefix=$root/bionic-prefix
headers=${BB_ARM64_HEADERS:-$root/headers}
toolchain=$ndk/build/cmake/android.toolchain.cmake
llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64
J=${BB_JOBS:-$(nproc)}
run() { nice -n 19 "$@"; }
[[ -f $toolchain ]] || { echo "No NDK: $ndk (tools/android/build_bionic_deps.sh)" >&2; exit 1; }
[[ -f $prefix/.done-ffmpeg ]] || { echo "No Bionic deps in $prefix (tools/android/build_bionic_deps.sh)" >&2; exit 1; }
[[ -f $headers/include/boost/version.hpp ]] || { echo "No Boost headers in $headers/include" >&2; exit 1; }
mkdir -p "$out"
ndk_cmake=(-DCMAKE_TOOLCHAIN_FILE="$toolchain" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$api -DANDROID_STL=c++_shared)
# This port's changes to FSR-Vulkan (gpu/patches/fsr-vulkan), applied once, as build.sh does.
for patch in gpu/patches/fsr-vulkan/*.patch; do
    if ! git -C gpu/third_party/fsr-vulkan apply --reverse --check "$PWD/$patch" 2>/dev/null; then
        git -C gpu/third_party/fsr-vulkan apply "$PWD/$patch"
    fi
done

# FEX: no glibc jemalloc hook (glibc-only; it exists for thunks, which this build does not use).
what=${1:-all}
if [[ $what == all || $what == cpu ]]; then
    if [[ ! -f $out/fex/build.ninja ]]; then
        cmake -S third_party/FEX -B "$out/fex" -G Ninja "${ndk_cmake[@]}" \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_PROJECT_FEX_INCLUDE="$PWD/cpu/fex_project.cmake" \
            -DBUILD_TESTING=OFF -DBUILD_THUNKS=OFF -DBUILD_FEXCONFIG=OFF -DBUILD_STEAM_SUPPORT=OFF \
            -DBUILD_FEX_LINUX_TESTS=OFF -DENABLE_ZYDIS=OFF -DTUNE_ARCH=armv8.2-a -DTUNE_CPU=generic \
            -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF >/dev/null
    fi
    run ninja -C "$out/fex" -j "$J" bbcpu > "$out/fex-build.log" 2>&1 || { grep -E "error|FAILED" "$out/fex-build.log" | head -40 >&2; exit 1; }
fi

# GPU library: no libvulkan (gpu/shim/vk_driver_loader.cpp dlopens the driver), no X11, no SDL,
# no online module (BB_NET=OFF: FEXCore builds always play offline). glslang (host program)
# compiles the host shaders.
if [[ $what == all || $what == gpu ]]; then
    export PKG_CONFIG_PATH=$prefix/lib/pkgconfig:$prefix/share/pkgconfig
    export PKG_CONFIG_LIBDIR=$PKG_CONFIG_PATH
    if [[ ! -f $out/gpu/build.ninja ]]; then
        cmake -S gpu -B "$out/gpu" -G Ninja "${ndk_cmake[@]}" \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBB_PGO=off -DBB_LTO=OFF -DBB_NET=OFF \
            "-DCMAKE_FIND_ROOT_PATH=$prefix;$headers" -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
            -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
            -DCMAKE_PREFIX_PATH="$prefix;$headers" \
            -DBoost_INCLUDE_DIR="$headers/include" \
            "-DBB_CPU_LIB=$PWD/$out/fex/libbbcpu.so" >/dev/null
    fi
    run ninja -C "$out/gpu" -j "$J" bbgpu > "$out/gpu-build.log" 2>&1 || { grep -v '^\[' "$out/gpu-build.log" | grep -B2 -A12 'error' | head -80 >&2; exit 1; }
fi

if [[ $what == all || $what == probe ]]; then
    # bin/ and lib/ side by side, executables find lib/ through $ORIGIN.
    mkdir -p "$out/bin" "$out/lib"
    cp -f "$out/fex/libbbcpu.so" "$out/gpu/libbbgpu.so" "$out/lib/"
    # bb-probe keeps SDL3 for its helpers (math, key names; no device is opened on Android).
    cp -f "$prefix/lib/libSDL3.so" "$out/lib/"
    cp -f "$llvm/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$out/lib/"

    cc=("$llvm/bin/aarch64-linux-android$api-clang" -fuse-ld=lld)
    mkdir -p "$out/atrac9"
    for source in third_party/LibAtrac9/C/src/*.c; do
        "${cc[@]}" -std=c99 -O2 -g -w -fPIC -c "$source" -o "$out/atrac9/$(basename "${source%.c}").o"
    done
    "$llvm/bin/llvm-ar" rcs "$out/libatrac9.a" "$out"/atrac9/*.o
    # PIE (Android requires it; the PC build is -no-pie for its brk heap, see src/probe.c main),
    # -rdynamic: libbbgpu resolves runtime_* symbols against the executable. Linked against the NDK
    # sysroot only: no libvulkan, no loader of ours.
    # runtime_ajm.c's MP3 decoder: the static FFmpeg of the deps (libbbgpu keeps its own copy hidden).
    read -r -a ffmpeg <<< "$(PKG_CONFIG_PATH=$prefix/lib/pkgconfig PKG_CONFIG_LIBDIR=$prefix/lib/pkgconfig \
        pkg-config --static --libs libavcodec libavutil)"
    links=(-L"$out/lib" -lbbgpu -lbbcpu -lSDL3 "${ffmpeg[@]}" -Wl,--exclude-libs,libavcodec.a:libavutil.a:libswresample.a
           -Wl,-rpath,'$ORIGIN/../lib' -rdynamic -pie)
    "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread -I"$prefix/include" -I. -Isrc \
        src/probe.c src/runtime*.c src/vulkan_smoke.c "$out/libatrac9.a" -lm "${links[@]}" \
        -o "$out/bin/bb-probe"
    "${cc[@]}" -std=c11 -O2 -Wall -Wextra -Werror tools/android/kgsl_chip_id.c -pie -o "$out/bin/kgsl-chip-id"
    echo "Built $out/bin/bb-probe, $out/lib/libbbgpu.so, $out/lib/libbbcpu.so (Bionic, API $api)"
fi
