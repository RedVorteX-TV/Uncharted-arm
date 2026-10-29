#!/usr/bin/env bash
# Linux fork: builds dist/uncharted-linux.tar.gz after `bash build.sh`. Uncharted is the
# launcher (frozen with PyInstaller, so no Python is needed), bin/ holds bb-probe, its GPU library
# and the libraries a Steam Deck or another distribution may lack (bin/lib), the preparation
# scripts and run.py. Graphics-driver, glibc and libstdc++ libraries come from the system.
# LINUX_BUILD (e.g. 0.3.1-linux.4) is written into the package for the launcher's update check.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
[[ -f out/bb-probe && -f out/gpu/libbbgpu.so && -f out/bb-gpu-capabilities ]] || { echo 'Build first: bash build.sh' >&2; exit 1; }
# ARM port: aarch64 builds (packaging/armada/build.sh) add the guest CPU library, FEXCore's
# out/fex/libbbcpu.so, which bb-probe finds in bin/cpu ($ORIGIN/cpu).
arch=$(uname -m)
if [[ $arch == aarch64 && ! -f out/fex/libbbcpu.so ]]; then echo 'Build first: bash packaging/armada/build.sh' >&2; exit 1; fi

python=${PYTHON:-python3}
if [[ ! -x out/pyenv/bin/python ]]; then
    "$python" -m venv out/pyenv
fi
out/pyenv/bin/python -m pip install -q --disable-pip-version-check pyinstaller pillow
[[ -f launcher/uncharted.png ]] || out/pyenv/bin/python - <<'EOF'
from PIL import Image
icon = Image.open('launcher/uncharted.ico')
icon.size = max(icon.ico.sizes())
icon.convert('RGBA').save('launcher/uncharted.png')
EOF
hidden=()
for module in argparse base64 collections hashlib json re shlex shutil struct tempfile zlib \
              uncharted_art uncharted_fonts uncharted_widgets uncharted_linux uncharted_logs uncharted_i18n bbport_steam PIL.ImageTk PIL._tkinter_finder; do
    hidden+=(--hidden-import "$module")
done
out/pyenv/bin/python -m PyInstaller --noconfirm --clean --log-level WARN --windowed \
    --name Uncharted --distpath out/pyi-dist --workpath out/pyi-work --specpath out/pyi-work \
    --paths "$PWD/scripts" --paths "$PWD/launcher" \
    --add-data "$PWD/launcher/fonts:fonts" --add-data "$PWD/launcher/covers:covers" \
    --add-data "$PWD/launcher/uncharted.png:." --add-data "$PWD/launcher/icons:icons" \
    "${hidden[@]}" launcher/uncharted_launcher.py

dest=out/stage/uncharted-linux
rm -rf -- out/stage
mkdir -p "$dest/bin/gpu" "$dest/bin/lib" "$dest/launcher" "$dest/scripts" "$dest/licenses"
cp -r out/pyi-dist/Uncharted/. "$dest/"
strip --strip-debug -o "$dest/bin/bb-probe" out/bb-probe
strip --strip-debug -o "$dest/bin/bb-gpu-capabilities" out/bb-gpu-capabilities
strip --strip-debug -o "$dest/bin/gpu/libbbgpu.so" out/gpu/libbbgpu.so
if [[ $arch == aarch64 ]]; then
    mkdir -p "$dest/bin/cpu"
    strip --strip-debug -o "$dest/bin/cpu/libbbcpu.so" out/fex/libbbcpu.so
fi
# Linux fork: NVIDIA DLSS (DLAA) when packaging/linux/build_dlss.sh has put the bridge and the
# SDK's DLSS library in out/dlss: next to bb-probe, where vk_dlss.cpp looks for them.
if [[ -f out/dlss/libbbport_dlss.so ]] && compgen -G 'out/dlss/libnvidia-ngx-dlss.so.*' >/dev/null; then
    cp out/dlss/libbbport_dlss.so out/dlss/libnvidia-ngx-dlss*.so.* "$dest/bin/"
    if [[ -f out/dlss/LICENSE-DLSS-SDK.txt ]]; then cp out/dlss/LICENSE-DLSS-SDK.txt "$dest/licenses/"; fi
    echo 'DLSS: bridge and NVIDIA DLSS libraries packaged'
fi
# FSR 4 assets (tools/fetch_fsr4_assets.sh) when present: run.py starts the game in the package
# folder, where vk_fsr4.cpp looks for fsr4_shaders/ by default.
if [[ -d fsr4_shaders ]]; then cp -r fsr4_shaders "$dest/"; echo 'FSR 4: assets packaged'; fi
# Libraries every desktop and SteamOS have, or that must match the graphics driver, stay out
# (whole families: libX*, libdrm_*, libpulse*, ...). libva is bundled: the bundled ffmpeg (Debian 13)
# needs libva 2.21+ (vaMapBuffer2) and Ubuntu 24.04 / Mint 22 / Pop!_OS 24.04 have 2.20, so no game
# started there. The game decodes in software and never loads a VA driver.
system='^(ld-linux|libc\.|libm\.|libdl\.|libpthread|librt\.|libresolv|libutil\.|libgcc_s|libstdc\+\+|libvulkan|libX|libxcb|libxkb|libwayland|libdrm|libGL|libEGL|libgbm|libasound|libpulse|libpipewire|libdbus|libudev|libsystemd|libz\.|libzstd|libexpat|libelf|libLLVM|libffi|libcap\.|libgcrypt|libgpg-error|liblzma|libmount|libblkid|libglib|libgobject|libgio|libgmodule|libpcre2|libdecor|libxshmfence|libsensors|libfontconfig|libfreetype)'
probe_libraries=(out/bb-probe out/gpu/libbbgpu.so out/bb-gpu-capabilities)
if [[ $arch == aarch64 ]]; then probe_libraries+=(out/fex/libbbcpu.so); fi
ldd "${probe_libraries[@]}" | awk '/=> \// {print $1, $3}' | sort -u |
    while read -r name path; do
        # Arch and SteamOS list the loader as /lib64/ld-linux-x86-64.so.2 => ...: match by file name.
        name=$(basename -- "$name")
        # The GPU library is bin/gpu's stripped copy (bb-probe's RUNPATH): an unstripped second
        # copy here, first on LD_LIBRARY_PATH, was the one loaded and added 60 MB to the AppImage.
        [[ $name == libbbgpu.so || $name == libbbcpu.so ]] && continue
        [[ $name =~ $system ]] || cp -L "$path" "$dest/bin/lib/$name"
    done
cp scripts/prepare.py scripts/link_modules.py scripts/link_libc.py scripts/content_profile.py scripts/render_720p.py scripts/render_res.py scripts/graphics_patches.py "$dest/scripts/"
mkdir -p "$dest/patches" && cp patches/graphics_patches.json "$dest/patches/"
cp run.py LICENSE COPYING.GPL-3 "$dest/"  # GPL v3: the binaries include the LSFG files
cp launcher/uncharted.png "$dest/launcher/"
cp README.md "$dest/README.md"
echo "${LINUX_BUILD:-}" > "$dest/LINUX_BUILD"
# A custom build that must never update itself to an official release (NO_UPDATE_CHECK=1).
if [[ ${NO_UPDATE_CHECK:-0} == 1 ]]; then : > "$dest/NO_UPDATE_CHECK"; fi
for license in gpu/third_party/httplib/LICENSE:cpp-httplib launcher/fonts/OFL.txt:Cinzel; do
    if [[ -f ${license%%:*} ]]; then cp "${license%%:*}" "$dest/licenses/${license##*:}-LICENSE.txt"; fi
done
# The bundled libraries' own copyright files (Debian keeps them per package; Fedora, which the
# ARM build container runs, in /usr/share/licenses/<package>).
for library in "$dest"/bin/lib/*; do
    if command -v dpkg >/dev/null; then
        package=$(dpkg -S "$(readlink -f "/usr/lib/$arch-linux-gnu/$(basename "$library")" 2>/dev/null || echo none)" 2>/dev/null | cut -d: -f1 | head -1 || true)
        if [[ -n $package && -f /usr/share/doc/$package/copyright ]]; then
            cp "/usr/share/doc/$package/copyright" "$dest/licenses/$package-copyright.txt"
        fi
    elif command -v rpm >/dev/null; then
        source=$(ldconfig -p | awk -v n="$(basename "$library")" '$1 == n { print $NF; exit }')
        package=
        if [[ -n $source ]]; then package=$(rpm -qf --qf '%{NAME}\n' "$source" 2>/dev/null | head -1 || true); fi
        if [[ -n $package && -d /usr/share/licenses/$package ]]; then
            for file in /usr/share/licenses/"$package"/*; do
                if [[ -f $file ]]; then cp "$file" "$dest/licenses/$package-$(basename "$file")"; fi
            done
        fi
    fi
done
# The newest glibc symbol version the packaged programs and libraries use: AppRun says so on an
# older system instead of a loader error.
# (objdump fails on the scripts among them: that is not an error here.)
{ find "$dest" -type f \( -name '*.so*' -o -perm -u+x \) -print0 | xargs -0 objdump -T 2>/dev/null || true; } |
    { grep -o 'GLIBC_[0-9][0-9.]*' || true; } | sed 's/^GLIBC_//' | sort -uV | tail -1 > "$dest/GLIBC_REQUIRED"
echo "Needs glibc $(cat "$dest/GLIBC_REQUIRED") or newer"
find "$dest" -name __pycache__ -prune -exec rm -r {} +
mkdir -p dist
tar -C out/stage -czf dist/uncharted-linux.tar.gz uncharted-linux

# The AppImage: the same files in one executable. Saves, settings and generated files go to
# ~/.local/share/uncharted-linux, since the AppImage itself is read-only.
appdir=out/stage/Uncharted.AppDir
cp -a "$dest" "$appdir"
cp packaging/linux/AppRun "$appdir/AppRun"
chmod +x "$appdir/AppRun"
cp packaging/linux/uncharted-linux.desktop "$appdir/"
cp launcher/uncharted.png "$appdir/uncharted-linux.png"
cp launcher/uncharted.png "$appdir/.DirIcon"
tool=out/appimagetool-1.9.1-$arch
if [[ ! -x $tool ]]; then
    curl -fsSL -o "$tool" "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-$arch.AppImage"
    chmod +x "$tool"
fi
# Extract-and-run: works without FUSE (containers, CI).
# APPIMAGE_NAME: another file name (a custom build next to the official one).
appimage=dist/${APPIMAGE_NAME:-Uncharted-$arch.AppImage}
ARCH=$arch APPIMAGE_EXTRACT_AND_RUN=1 "$tool" --no-appstream "$appdir" "$appimage" >/dev/null
ls -lh dist/uncharted-linux.tar.gz "$appimage"
