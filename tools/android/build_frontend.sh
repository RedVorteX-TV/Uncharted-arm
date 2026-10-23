#!/usr/bin/env bash
# The Uncharted Android APK (adapted from ducvd89's Bloodborne-Android build_frontend.sh): the Java app,
# libbbpresent.so (the frame path), the bundled GPU drivers and the runtime archive (out/bionic from
# build_bionic.sh and build_prepare.sh, with run-uncharted.sh) as assets. No Gradle: javac, d8, aapt.
#
#   bash tools/android/build_frontend.sh           -> out/android/Uncharted-<version>.apk
#
# BB_KEYSTORE: the signing key (default ~/.android/uncharted-test.keystore, made when missing; never in
# the repository). BB_SKIP_DRIVERS=1: no bundled drivers (the app then uses the phone's own driver).
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
sdk=${BB_ANDROID_SDK:-"$PWD/.local-deps/android/sdk"}
build_tools="$sdk/build-tools/35.0.0"
android_jar="$sdk/platforms/android-35/android.jar"
ndk=${BB_NDK:-$sdk/ndk/29.0.14206865}
llvm=$ndk/toolchains/llvm/prebuilt/linux-x86_64
app=tools/android/frontend
out="$PWD/out/android/app"
version=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' "$app/AndroidManifest.xml")
rm -rf "$out"
mkdir -p "$out/classes" "$out/dex" "$out/res" "$out/assets" "$out/lib/arm64-v8a" "$out/runtime/bbport"

# ---- The runtime archive: everything the app installs into files/bbport (RuntimeInstaller) ----
stage="$out/runtime/bbport"
mkdir -p "$stage/bionic/bin" "$stage/bionic/lib" "$stage/data" "$stage/logs" "$stage/user" "$stage/licenses"
for program in bb-probe bb-prepare kgsl-chip-id; do
    [[ -x out/bionic/bin/$program ]] || { echo "Missing out/bionic/bin/$program: build_bionic.sh / build_prepare.sh android" >&2; exit 1; }
    cp out/bionic/bin/$program "$stage/bionic/bin/"
done
for library in libbbgpu.so libbbcpu.so libSDL3.so libc++_shared.so; do
    [[ -f out/bionic/lib/$library ]] || { echo "Missing out/bionic/lib/$library: build_bionic.sh" >&2; exit 1; }
    cp out/bionic/lib/$library "$stage/bionic/lib/"
done
"$llvm/bin/llvm-strip" --strip-debug "$stage"/bionic/bin/* "$stage"/bionic/lib/*
chmod 755 "$stage"/bionic/bin/* "$stage"/bionic/lib/*
cp tools/android/run-uncharted.sh "$stage/run-uncharted.sh"
chmod 755 "$stage/run-uncharted.sh"
build=$(git describe --always --dirty 2>/dev/null || echo source)
echo "android-$build" > "$stage/BUILD"
cp "$app/THIRD_PARTY_LICENSES.txt" "$stage/licenses/"
# Words that must not appear in the archive: game-derived files.
if (cd "$out/runtime" && find . | grep -E 'eboot|boot-linked|\.elf$|\.prx$|sce_sys'); then
    echo "Game files in the runtime archive" >&2; exit 1
fi
(cd "$out/runtime" && tar --owner=0 --group=0 --numeric-owner -czf "$out/assets/runtime.payload" bbport)
sha256sum "$out/assets/runtime.payload" | cut -d' ' -f1 > "$out/assets/runtime.sha256"

# ---- The GPU drivers inside the APK (AutoDriver.java, BundledDrivers.java) ----
cp -r "$app/res/." "$out/res/"
drivers=$PWD/.local-deps/android/drivers
if [[ ${BB_SKIP_DRIVERS:-} != 1 ]]; then
    bash tools/android/fetch_bundled_drivers.sh >/dev/null \
        || { echo "The bundled GPU drivers are missing or do not match their sha256 (BB_SKIP_DRIVERS=1 builds without them)" >&2; exit 1; }
    mkdir -p "$out/assets/drivers"
    cp "$drivers"/Turnip_Gen8_V37.zip "$drivers"/PanVK-android-v3.2-universal-2a9358a.zip "$out/assets/drivers/"
    { cat "$app/THIRD_PARTY_LICENSES.txt" "$app/DRIVERS_NOTICE.txt"
      unzip -p "$drivers/Turnip-android-main-26.3.0-35b085c.zip" LICENSE-mesa.txt
      printf '\n--------------------------------------------------------------------------------\nGNU General Public License, version 3 (this app'"'"'s runtime; StevenMXZ/Adreno-Tools-Drivers)\n--------------------------------------------------------------------------------\n\n'
      cat "$drivers/GPL-3.0.txt"
    } > "$out/assets/THIRD_PARTY_LICENSES.txt"
else
    { cat "$app/THIRD_PARTY_LICENSES.txt"
      printf '\n--------------------------------------------------------------------------------\nGNU General Public License, version 3 (this app'"'"'s runtime)\n--------------------------------------------------------------------------------\n\n'
      cat COPYING.GPL-3
    } > "$out/assets/THIRD_PARTY_LICENSES.txt"
fi

# ---- libbbpresent.so: the game's frame path (AHardwareBuffers -> ASurfaceControl) ----
"$llvm/bin/aarch64-linux-android28-clang" -fuse-ld=lld -std=c11 -O2 -g \
    -Wall -Wextra -Werror -fPIC -shared -Wl,-soname,libbbpresent.so -Wl,-z,max-page-size=16384 \
    -Itools/android/present "$app/jni/bbpresent/bbpresent.c" \
    -landroid -llog -lnativewindow -o "$out/lib/arm64-v8a/libbbpresent.so"

# ---- Java -> dex, resources, the APK ----
mapfile -t sources < <(find "$app/src" -name '*.java' | sort)
javac -encoding UTF-8 --release 8 -Xlint:-options -classpath "$android_jar" -d "$out/classes" "${sources[@]}"
mapfile -t classes < <(find "$out/classes" -name '*.class' | sort)
"$build_tools/d8" --release --min-api 28 --lib "$android_jar" --output "$out/dex" "${classes[@]}"
"$build_tools/aapt" package -f -M "$app/AndroidManifest.xml" \
    -S "$out/res" -A "$out/assets" -I "$android_jar" -0 payload -0 zip -F "$out/unsigned.apk"
(cd "$out/dex" && zip -q "$out/unsigned.apk" ./*.dex)
(cd "$out" && zip -q -r unsigned.apk lib)
"$build_tools/zipalign" -f -p 16 "$out/unsigned.apk" "$out/aligned.apk"
key=${BB_KEYSTORE:-$HOME/.android/uncharted-test.keystore}
if [[ ! -f $key ]]; then
    mkdir -p "$(dirname "$key")"
    keytool -genkeypair -keystore "$key" -storepass android -keypass android \
        -alias uncharted -dname 'CN=Uncharted ARM port test build' -keyalg RSA -keysize 2048 -validity 10000
fi
apk="$PWD/out/android/Uncharted-$version.apk"
"$build_tools/apksigner" sign --ks "$key" --ks-pass pass:android --key-pass pass:android --out "$apk" "$out/aligned.apk"
"$build_tools/apksigner" verify "$apk"
echo "Built $apk"
