#!/usr/bin/env bash
# Undefined dynamic symbols of the Android outputs that nothing provides (weak ones ignored).
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
out=out/bionic
llvm=${BB_NDK:-.local-deps/android/sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/bin
sys=${BB_NDK:-.local-deps/android/sdk/ndk/29.0.14206865}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/30
defined() { "$llvm/llvm-nm" -D --defined-only "$@" 2>/dev/null | awk 'NF>=3{print $3}' | sed 's/@.*//'; }
provided=$(mktemp)
{ defined $out/bin/bb-probe $out/lib/*.so; for l in libc.so libm.so libdl.so liblog.so libandroid.so libnativewindow.so; do defined $sys/$l; done; } | sort -u > $provided
fail=0
for f in $out/bin/bb-probe $out/bin/bb-prepare $out/bin/kgsl-chip-id $out/lib/libbbgpu.so $out/lib/libbbcpu.so; do
    need=$("$llvm/llvm-readelf" -d "$f" | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p' | tr '\n' ' ')
    missing=$("$llvm/llvm-nm" -D --undefined-only "$f" | awk '$1!="w"&&$1!="v"{print $2}' | sed 's/@.*//' | sort -u | comm -23 - $provided || true)
    echo "== $f  NEEDED: $need"
    [ -n "$missing" ] && { echo "$missing" | sed 's/^/   UNDEFINED /' | head -40; fail=1; }
done
rm -f $provided
exit $fail
