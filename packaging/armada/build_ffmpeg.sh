#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# FFmpeg for the ARM build (packaging/armada/container_build.sh), static and position independent,
# into out/ffmpeg. Fedora's ffmpeg-free has no native H.264/HEVC decoder (OpenH264 only, one thread
# and slow with High profile): the games' movies (prerecorded cinematics) fell behind their sound.
# The same release and options as the Android build (tools/android/build_bionic_deps.sh on the
# android branch): FFmpeg's own decoders, demuxers and NEON code, nothing external.
#
#   bash packaging/armada/build_ffmpeg.sh    -> out/ffmpeg/{include,lib/*.a,lib/pkgconfig}
set -euo pipefail
cd -- "$(dirname -- "$(realpath -- "${BASH_SOURCE[0]}")")/../.."
version=7.1.5
sha256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
prefix=$PWD/out/ffmpeg
src=$PWD/out/ffmpeg-src
[[ -f $prefix/.done-$version ]] && exit 0
rm -rf "$prefix" "$src" && mkdir -p "$src"
archive=$src/ffmpeg-$version.tar.xz
curl -fsSL "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" -o "$archive"
echo "$sha256  $archive" | sha256sum -c --quiet - || { echo "ffmpeg-$version.tar.xz: checksum mismatch" >&2; exit 1; }
tar -xf "$archive" -C "$src" --strip-components=1
(cd "$src" && ./configure --prefix="$prefix" --cc=clang --cxx=clang++ \
    --enable-static --disable-shared --enable-pic --disable-programs --disable-doc --disable-debug \
    --disable-avdevice --disable-avfilter --disable-network --disable-autodetect > configure.log 2>&1) \
    || { tail -30 "$src/configure.log" >&2; exit 1; }
make -C "$src" -j"$(nproc)" > "$src/build.log" 2>&1 || { tail -30 "$src/build.log" >&2; exit 1; }
make -C "$src" install > /dev/null
rm -rf "$src"
touch "$prefix/.done-$version"
echo "FFmpeg $version (static) in $prefix"
