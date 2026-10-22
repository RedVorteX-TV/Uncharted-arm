#!/usr/bin/env bash
# Turnip (Mesa's Adreno Vulkan driver) for Android (Bionic): KGSL only, cross-compiled with the
# NDK, for the Bionic game process, which dlopen()s libvulkan_freedreno.so and calls
# vk_icdGetInstanceProcAddr. Two variants, as in build_turnip.sh (patches: mesa-patches/README.md):
#   main: upstream Mesa at the pinned commit (Adreno 6xx/7xx; 8xx too, as far as Mesa main has it)
#   gen8: whitebelyash/mesa-unified turnip/gen8 (Adreno 8xx) + mesa-patches/gen8
# Our patches: 0001 (A840 patch-id wildcard), 0006 (power constraint from env), 0008/0009
# (in-process opaque-fd timelines for lsfg-vk). 0005 and 0007 (X11 software WSI) are Linux-only
# and are not applied. Patches that do not apply as they are live in mesa-patches/android/ and
# replace their namesake.
# Output: out/bionic/turnip-<variant>/{libvulkan_freedreno.so,BUILD-INFO.json}
# Packaging: release_turnip_android.py.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
variant=${1:-main}
root=$PWD
P=tools/android/mesa-patches
case $variant in
    main)
        repo=https://gitlab.freedesktop.org/mesa/mesa.git
        MESA_COMMIT=${MESA_COMMIT:-35b085c4c067f198ce0b6437d94678e47b9c645f}
        names=(0001-freedreno-a840-kgsl-patchid-wildcard.patch
               0006-tu-kgsl-power-constraint-from-the-environment.patch
               0008-vulkan-in-process-opaque-fd-for-emulated-timelines.patch
               0009-tu-kgsl-wake-pending-syncobj-waits-on-any-device-submit.patch
               0010-android-stub-lazy-hw_get_module-without-libhardware-needed.patch
               0011-vulkan-android-export-the-icd-entry-points.patch)
        pre=() ;;
    gen8)
        repo=https://github.com/whitebelyash/mesa-unified.git
        MESA_COMMIT=${MESA_COMMIT:-9c7e022677dfa3abb356c2b6732cbd2e25783d01}
        names=(0001-freedreno-a840-kgsl-patchid-wildcard.patch
               0006-tu-kgsl-power-constraint-from-the-environment.patch
               0008-vulkan-in-process-opaque-fd-for-emulated-timelines.patch
               0009-tu-kgsl-wake-pending-syncobj-waits-on-any-device-submit.patch
               0010-android-stub-lazy-hw_get_module-without-libhardware-needed.patch
               0011-vulkan-android-export-the-icd-entry-points.patch)
        pre=("$P"/gen8/*.patch) ;;
    *) echo 'usage: build_turnip_android.sh [main|gen8]' >&2; exit 2 ;;
esac
patches=("${pre[@]}")
for n in "${names[@]}"; do
    if [[ -f $P/android/$n ]]; then patches+=("$P/android/$n"); else patches+=("$P/$n"); fi
done

deps=$PWD/.local-deps/android
ndk=${BB_NDK:-$deps/sdk/ndk/29.0.14206865}
api=${BB_ANDROID_API:-30}
J=${BB_JOBS:-7}   # at most 7: other builds run on this machine
llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64
[[ -x $llvm/bin/aarch64-linux-android$api-clang ]] || { echo "No NDK: $ndk" >&2; exit 1; }
src=$deps/src/mesa-android-$variant
build=$PWD/out/bionic/turnip-$variant-build
out=$PWD/out/bionic/turnip-$variant
run() { taskset -c 2-23 nice -n 19 "$@"; }

venv=$deps/mesa-venv
if [[ ! -x $venv/bin/meson ]]; then
    python3 -m venv "$venv"
    "$venv/bin/python" -m pip install -q meson mako pyyaml packaging
fi
export PATH="$venv/bin:$PATH"

# A pristine tree at the pinned commit (fetched from MESA_LOCAL_REPO when it has it), then patches.
if [[ ! -d $src/.git ]]; then
    git init -q "$src"
    git -C "$src" remote add origin "$repo"
fi
local_repo=${MESA_LOCAL_REPO:-}
if ! git -C "$src" cat-file -e "$MESA_COMMIT^{commit}" 2>/dev/null; then
    { [[ -d $local_repo/.git ]] && git -C "$src" fetch -q --depth 1 "$local_repo" "$MESA_COMMIT"; } 2>/dev/null \
        || git -C "$src" fetch -q --depth 1 origin "$MESA_COMMIT"
fi
git -C "$src" checkout -q --force "$MESA_COMMIT"
git -C "$src" clean -q -fdx
for patch in "${patches[@]}"; do
    git -C "$src" apply "$root/$patch"
done

mkdir -p "$(dirname "$build")"
cross=$build.cross
cat > "$cross" <<EOT
[binaries]
c = ['$llvm/bin/aarch64-linux-android$api-clang']
cpp = ['$llvm/bin/aarch64-linux-android$api-clang++', '-static-libstdc++']
c_ld = 'lld'
cpp_ld = 'lld'
ar = '$llvm/bin/llvm-ar'
strip = '$llvm/bin/llvm-strip'
ranlib = '$llvm/bin/llvm-ranlib'
pkg-config = 'pkg-config'
[built-in options]
c_args = ['-O2', '-fno-semantic-interposition']
cpp_args = ['-O2', '-fno-semantic-interposition', '-Wno-c++11-narrowing']
c_link_args = ['-Wl,-z,max-page-size=16384', '-Wl,--build-id=none']
cpp_link_args = ['-Wl,-z,max-page-size=16384', '-Wl,--build-id=none', '-static-libstdc++']
[properties]
# No host pkg-config packages (SPIRV-Tools, libelf, udev ... of this PC) in an Android build.
pkg_config_libdir = '/nonexistent'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOT
rm -rf "$build" "$out"
# -Dexpat: the NDK has no libexpat; the wrap builds it static (no extra NEEDED).
run meson setup "$build" "$src" --cross-file "$cross" --prefix=/usr --libdir=lib \
    --wrap-mode=default --force-fallback-for=expat --buildtype=release -Db_ndebug=true -Db_lto=false \
    -Dplatforms=android -Dplatform-sdk-version=$api -Dandroid-stub=true -Dandroid-libbacktrace=disabled \
    -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dgallium-drivers= \
    -Dopengl=false -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Dgbm=disabled \
    -Dllvm=disabled -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
    -Dspirv-tools=disabled -Dzstd=disabled -Dexpat=enabled -Dvulkan-layers= -Dtools= -Dbuild-tests=false \
    > "$build.log" 2>&1 || { tail -40 "$build.log" >&2; exit 1; }
run ninja -C "$build" -j "$J" >> "$build.log" 2>&1 || { tail -40 "$build.log" >&2; exit 1; }
DESTDIR="$build/install" meson install -C "$build" --no-rebuild >> "$build.log" 2>&1

mkdir -p "$out"
library=libvulkan_freedreno.so
built=$(find "$build/install" -name "$library" | head -1)
[[ -n $built ]] || { echo "No $library installed" >&2; exit 1; }
"$llvm/bin/llvm-strip" --strip-all -o "$out/$library" "$built"
needed=$("$llvm/bin/llvm-readelf" -d "$out/$library" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')
echo "NEEDED: $needed"
for lib in $needed; do
    case $lib in libc.so|libm.so|libdl.so|liblog.so|libnativewindow.so|libsync.so|libz.so|libandroid.so) ;;
        *) echo "$library needs $lib, which is not an Android system library" >&2; exit 1 ;; esac
done
"$llvm/bin/llvm-nm" -D --defined-only "$out/$library" | grep -q ' vk_icdGetInstanceProcAddr$' \
    || { echo "$library does not export vk_icdGetInstanceProcAddr" >&2; exit 1; }
python3 - "$variant" "$repo" "$(git -C "$src" rev-parse HEAD)" "$(<"$src/VERSION")" "$library" \
    "$out" "$api" "${patches[@]}" <<'PY'
import datetime, hashlib, json, sys
variant, repo, commit, version, library, out, api, *patches = sys.argv[1:]
sha256 = lambda path: hashlib.sha256(open(path, 'rb').read()).hexdigest()
info = {
    'variant': variant, 'target': f'android-arm64-api{api}', 'mesaRepository': repo,
    'mesaCommit': commit, 'mesaVersion': version,
    'buildDate': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
    'patches': [{'file': p, 'sha256': sha256(p)} for p in patches],
    'library': library, 'librarySha256': sha256(f'{out}/{library}'),
}
with open(f'{out}/BUILD-INFO.json', 'w') as f:
    json.dump(info, f, indent=2)
    f.write('\n')
PY
echo "Turnip Android $variant: $out (Mesa $(git -C "$src" rev-parse --short HEAD) + ${#patches[@]} patches)"
