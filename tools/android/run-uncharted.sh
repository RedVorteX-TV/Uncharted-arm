#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Uncharted: The Nathan Drake Collection on Android (ARM port by RedVorteX-TV, on ducvd89's
# Bloodborne-Android runtime). Runs inside the app's sandbox, started by MainActivity.java with
# /system/bin/sh: bb-probe with libbbgpu/libbbcpu (an Android NDK build) from the app's data
# directory. It does what run.py does on Linux: prepares the chosen executable (bb-prepare), starts
# it, and when the game asks for another executable (the collection menu starting Uncharted 1, 2 or
# 3; LoadExec, exit status 101) starts that one.
# Only Android's own tools run here (toybox: sed, tail, cat, cp, mkdir, printf, rm, taskset).
#
# Environment from the app: FXD_FILES (the app's files), BB_GAME_DIR (the folder with eboot.bin),
# BB_EXE (eboot.bin = the collection menu, u2, u3), BB_SCREEN_W/BB_SCREEN_H (the surface's size),
# BB_GPU_DRIVER/BB_GPU_DRIVER_WHY (the automatic driver choice).
set -u
files=${FXD_FILES:-/data/data/com.redvortextv.uncharted/files}
base="$files/bbport"
bionic="$base/bionic"
script="$base/run-uncharted.sh"
export FXD_FILES="$files"
export PATH=/system/bin
export HOME="$files/home/bbport"
export TMPDIR="$files/tmp" XDG_RUNTIME_DIR="$files/tmp"
# FEXCore (libbbcpu) reads its Config.json from here (it runs only the guest's code).
export FEX_APP_CONFIG_LOCATION="$HOME/.fex-emu/"
export FEX_APP_DATA_LOCATION="$HOME/.fex-emu/"
export FEX_APP_CACHE_LOCATION="$HOME/.cache/fex-emu/"
mkdir -p "$HOME/.fex-emu" "$HOME/.cache/fex-emu" "$TMPDIR" "$base/user" "$base/logs" "$base/data"
[ -f "$base/fex-config.json" ] && cp "$base/fex-config.json" "$HOME/.fex-emu/Config.json"

# ---- Settings: bbport.ini (the app's launcher and the in-game menu write it) ----
export BB_CONFIG="$base/bbport.ini"
[ -f "$BB_CONFIG" ] || : > "$BB_CONFIG"
setting() { sed -n "s/^$1=//p" "$BB_CONFIG" 2>/dev/null | tail -n 1 | tr -d '\r '; }
# The launcher's own choices (compatibility switches, GPU driver): launcher.ini, which the in-game menu
# never rewrites (it rewrites bbport.ini whole).
launcher_ini="$base/launcher.ini"
choice() { sed -n "s/^$1=//p" "$launcher_ini" 2>/dev/null | tail -n 1 | tr -d '\r '; }
# Input: the app's pad state (PadBridge.java) and text requests (its touch keyboard).
export BB_ANDROID_INPUT=1
export BB_PAD_FILE="$base/android-pad.state" BB_PAD_QUIET=1
export BB_IME_FILE="$base/ime.request"
# The display: frames go to the app (BB_PRESENT_SOCKET, BbPresent); the game's frame is scaled to the
# surface (BB_WINDOW_SIZE), with FSR 1 when fsr1=1 (bbport.ini, read by libbbgpu).
export BB_PRESENT_SOCKET="$files/bbpresent.sock"
export BB_SCREEN_W="${BB_SCREEN_W:-1920}" BB_SCREEN_H="${BB_SCREEN_H:-1080}"
export BB_WINDOW_SIZE="${BB_SCREEN_W}x${BB_SCREEN_H}" BB_FULLSCREEN=1
# The game's display rate stays 60 Hz; the FPS limit is fps_limit in bbport.ini (libbbgpu reads it,
# the in-game menu changes it). launcher.ini game_rate=120 (120 Hz screens): a frame that misses
# 33 ms is shown at 41.7 ms instead of waiting for 50.
if [ "$(choice game_rate)" = 120 ]; then export BB_VBLANK_HZ=120; else export BB_VBLANK_HZ=60; fi
case $(setting fps_limit) in 30|40|45|60|0) ;; *) printf 'fps_limit=40\n' >> "$BB_CONFIG" ;; esac
case $(setting render_720p) in 0|1) ;; *) printf 'render_720p=1\n' >> "$BB_CONFIG" ;; esac
case $(setting render_540p) in 0|1) ;; *) printf 'render_540p=0\n' >> "$BB_CONFIG" ;; esac
case $(setting fsr1) in 0|1) ;; *) printf 'fsr1=1\n' >> "$BB_CONFIG" ;; esac
# Temporal anti-aliasing / FSR 3.1 off unless asked for, as the Linux launcher sets it (libbbgpu's
# own default is FSR 3.1 with object motion, costly on these GPUs).
export BB_UPSCALER=${BB_UPSCALER:-off}
# Compatibility (the launcher's switches; each one off with its variable when a scene misbehaves).
[ "$(choice arm_uma_readback)" = 0 ] && export BB_UMA_READBACK=0
[ "$(choice arm_flush_draws)" = 0 ] && export BB_FLUSH_DRAWS=0
[ "$(choice arm_job_spin)" = 0 ] && export BB_JOB_SPIN=0
# Batched skinning (vk_skin_batch.cpp) runs with the two-stage draw pipeline: on at every render
# resolution unless the launcher's switch is off (then the pipeline only at 540p, unbatched).
if [ "$(choice arm_skin_batch)" = 0 ]; then export BB_SKIN_BATCH=0; else export BB_DRAW_PIPE=${BB_DRAW_PIPE:-1}; fi
# The launcher's "Performance statistics in the log": frame, thread, GPU wait and memory statistics.
[ "$(choice perf_stats)" = 1 ] && export BB_FRAME_STATS=1 BB_THREAD_STATS=1
# Sound: the mixer writes s16 stereo 48 kHz into the FIFO the app plays (AudioBridge.java), only when
# the app made it (a FIFO nobody reads would fill).
[ -p "$base/audio.fifo" ] && export BB_AUDIO_FIFO="$base/audio.fifo"
# The collection's renderer (all three games), as run.py sets it (docs/upscaler.md).
export BB_VELOCITY_PS=${BB_VELOCITY_PS:-f4b18264} BB_UPSCALE_BEFORE_CS=${BB_UPSCALE_BEFORE_CS:-aec539f0}
export BB_HUDLESS_PS=${BB_HUDLESS_PS:-484cdbf5}
# Saves: one folder per game (runtime_savedata.c), GPU caches beside them.
user_dir="$base/user"
export BB_SAVEDATA_DIRS="SAVEDATA01=Uncharted 1;SAVEDATA02=Uncharted 2;SAVEDATA03=Uncharted 3"
export BB_GPU_USER_DIR="$user_dir"
export BB_FAILED_SHADER_DIR="$base/logs/failed-shaders"
mkdir -p "$BB_FAILED_SHADER_DIR"

# ---- The GPU driver (launcher.ini gpu_driver; from ducvd89's run-bionic.sh) ----
# "system" is the phone's stock driver through its Vulkan loader (BB_VULKAN_DRIVER unset); an id is
# files/drivers/<name>/ made by DriverManager.java, whose driver.lib names the library, loaded
# directly. Empty, "auto", or an id whose folder is gone: the driver the app resolved for this GPU
# (BB_GPU_DRIVER, AutoDriver.java), else the stock driver.
gpu_driver=$(choice gpu_driver)
gpu_auto=0
case $gpu_driver in
    ''|auto) gpu_auto=1; gpu_driver= ;;
    .*|*/*|system) gpu_driver= ;;
    *) if [ ! -f "$files/drivers/$gpu_driver/driver.lib" ]; then
           echo "GPU driver $gpu_driver is gone: automatic choice"; gpu_auto=1; gpu_driver=
       fi ;;
esac
if [ "$gpu_auto" = 1 ]; then
    gpu_pick=${BB_GPU_DRIVER:-}
    case $gpu_pick in ''|.*|*/*|system) gpu_pick= ;; esac
    if [ -n "$gpu_pick" ] && [ -f "$files/drivers/$gpu_pick/driver.lib" ]; then
        gpu_driver=$gpu_pick
        echo "GPU driver: automatic -> $gpu_pick (${BB_GPU_DRIVER_WHY:-no reason given})"
    else
        echo "GPU driver: automatic -> system (stock): ${BB_GPU_DRIVER_WHY:-the app did not name a driver}"
    fi
fi
unset BB_GPU_DRIVER BB_GPU_DRIVER_WHY BB_VULKAN_DRIVER
if [ -n "$gpu_driver" ]; then
    driver_lib=$(cat "$files/drivers/$gpu_driver/driver.lib" 2>/dev/null || true)
    case $driver_lib in
        ''|*/*|.*) echo "GPU driver $gpu_driver is not installed: system driver" ;;
        *) if [ -f "$files/drivers/$gpu_driver/$driver_lib" ]; then
               export BB_VULKAN_DRIVER="$files/drivers/$gpu_driver/$driver_lib"
               echo "GPU driver: $gpu_driver"
           else
               echo "GPU driver $gpu_driver has no library: system driver"
           fi ;;
    esac
fi
[ -n "${BB_VULKAN_DRIVER:-}" ] || echo 'GPU driver: system (stock)'

# Local overrides for this device (experiments, tuning): sourced last, not part of the runtime.
[ -f "$base/local.sh" ] && . "$base/local.sh"

cd "$base"
[ -x "$bionic/bin/bb-probe" ] || { echo "The runtime is missing: $bionic" >&2; exit 2; }
# The Bionic libraries only: the app's own environment has no library path.
LD_LIBRARY_PATH="$bionic/lib"; export LD_LIBRARY_PATH
unset LD_PRELOAD
if [ "${1:-game}" = vulkan ]; then exec "$bionic/bin/bb-probe" --vulkan-only; fi

# ---- The game: the collection menu, or Uncharted 2 / 3 directly; LoadExec between them ----
game_dir=${BB_GAME_DIR:-$base/game}
case ${BB_EXE:-} in
    u2|U2) exe=big2-ps4_Shipping.elf ;;
    u3|U3) exe=big3-ps4_Shipping.elf ;;
    ''|u1|U1|eboot.bin) exe=eboot.bin ;;
    *) exe=$BB_EXE ;;
esac
loadexec="$base/data/loadexec.txt"
export BB_LOADEXEC_FILE="$loadexec"
player_merge=${BB_GPU_MERGE-unset}
guest_argv=
echo "Run: Uncharted Android $(cat "$base/BUILD" 2>/dev/null || echo source), game $exe, $(uname -sr), screen ${BB_SCREEN_W}x${BB_SCREEN_H}, fps_limit $(setting fps_limit), render_720p $(setting render_720p), render_540p $(setting render_540p), fsr1 $(setting fsr1)"
while :; do
    [ -f "$game_dir/$exe" ] || { echo "Game missing: no $exe in $game_dir"; exit 2; }
    # Uncharted 2 and 3 read the results of GPU work back from memory: accurate GPU memory
    # (BB_GPU_MERGE=0), as run.py, unless set from outside.
    if [ "$player_merge" = unset ]; then
        case $exe in big2-*|big3-*) export BB_GPU_MERGE=0 ;; *) unset BB_GPU_MERGE ;; esac
    fi
    stem=${exe%%-*}; stem=${stem%.*}
    data="$base/data/$stem"
    "$bionic/bin/bb-prepare" "$game_dir" "$data" "$exe" || { echo "Preparing $exe failed"; exit 2; }
    export BB_EXE=$exe
    # The in-game menu's "Apply and restart": this script again, for the same executable.
    export BB_RESTART_COMMAND="exec /system/bin/sh '$script' game"
    set -- "$bionic/bin/bb-probe" "$data/boot-linked.bin" --content-profile "$data/content.bin" \
        --app0 "$game_dir" --user "$user_dir" --timeout "${BB_TIMEOUT:-0}"
    [ -n "$guest_argv" ] && set -- "$@" --guest-argv "$guest_argv"
    # Render resolution (bbport.ini, the launcher and the in-game menu): render_720p=1, the game's
    # own PS4 720p mode; else render_540p=1, the whole game at 960x540 (HUD, menus and movies too;
    # bb-prepare writes render-960x540-2.bin after scripts/render_res.py); else 1080p.
    unset BB_SCENE_SIZE BB_MOVIE_SIZE BB_VDEC_CONTENT BB_RENDER_RES BB_OUTPUT_RES BB_UI_COMPOSITE_PS BB_UI_TONEMAP_PS
    # Upscaling presets (bbport.ini upscaler=fsr3 or gsr2 with preset=1..4, the launcher and the
    # in-game menu; as run.py's, after BilalReloaded's Uncharted PC port): the game draws at
    # 1920x1080 / 1.5, 1.7, 2 or 3 (bb-prepare's render-WxH-2.bin), the upscaler brings each frame
    # to 1920x1080 at the copy into the display buffer (pixel shader 749bbec9; Uncharted 3's
    # prompts after its tonemap, 484cdbf5) and the HUD and menus are drawn at 1920x1080. They
    # replace the render resolution below.
    # No choice yet (bbport.ini without upscaler=): off, as the launcher shows it (the renderer's
    # own default is FSR 3.1 at the render resolution).
    if [ -z "$(setting upscaler)" ] && [ -z "${BB_UPSCALER+set}" ]; then export BB_UPSCALER=off; fi
    upscale_size= movie_patch=
    case "$(setting upscaler)" in
        fsr3|gsr2)
            case "$(setting preset)" in
                1) upscale_size=1280x720 ;;
                2) upscale_size=1136x636 ;;
                3) upscale_size=960x540 ;;
                4) upscale_size=640x360 ;;
            esac ;;
    esac
    if [ -n "$upscale_size" ] && [ ! -f "$data/render-$upscale_size-2.bin" ]; then
        echo "Upscaling preset: $upscale_size not available for $exe ($(cat "$data/render-$upscale_size.txt" 2>/dev/null))"
        upscale_size=
    fi
    if [ -n "$upscale_size" ]; then
        set -- "$@" --patches "$data/render-$upscale_size-2.bin"
        export BB_SCENE_SIZE=$upscale_size BB_RENDER_RES=$upscale_size BB_OUTPUT_RES=1920x1080
        export BB_UI_COMPOSITE_PS=749bbec9 BB_UI_TONEMAP_PS=484cdbf5
        export BB_VDEC_CONTENT=$upscale_size
        movie_patch=1
        export BB_DRAW_PIPE=${BB_DRAW_PIPE:-1}
        echo "Render resolution: $upscale_size, upscaled to 1920x1080 ($(setting upscaler) preset $(setting preset))"
    elif [ "$(setting render_720p)" = 1 ]; then
        if [ -f "$data/render-720p.bin" ]; then set -- "$@" --patches "$data/render-720p.bin"
        else echo "Render 720p: not available for $exe ($(cat "$data/render-720p.txt" 2>/dev/null)); rendering at 1080p"; fi
    elif [ "$(setting render_540p)" = 1 ]; then
        if [ -f "$data/render-960x540-2.bin" ]; then
            set -- "$@" --patches "$data/render-960x540-2.bin"
            export BB_SCENE_SIZE=960x540
            # The movies: 1080p frames with the picture in their top-left 960x540, the part the
            # game shows at 540p (Videodec2, from revvv991's uncharted-android).
            export BB_VDEC_CONTENT=960x540
            movie_patch=1
            # At 540p the Thor waits for its CPU, not its GPU: Uncharted 2's ~1300 compute fences
            # a frame each waited for the guest copies (40% of the GPU command thread's time); with
            # the draw pipeline they are signalled after the copies instead (liverpool.cpp).
            export BB_DRAW_PIPE=${BB_DRAW_PIPE:-1}
            echo "Render resolution: 960x540"
        else echo "Render 540p: not available for $exe ($(cat "$data/render-960x540.txt" 2>/dev/null)); rendering at 1080p"; fi
    fi
    # The launcher's graphics settings (launcher.ini; the Bachata S4+ catalog's patches): an effect
    # switched off there (fx_<effect>=0; on, the game's own, by default) and gfx_shadows=2/1
    # cascades. bb-prepare wrote gfx-<id>.bin for the patches whose original bytes match this
    # executable (graphics-2.txt). Each is optional (--extra-patches: a write that does not fit is
    # skipped, the game still starts).
    gfx_on=
    for pair in logos:skip_logos water_reflections:no_water_reflections ssao:no_ssao fog:no_fog \
                volumetric:no_volumetric motion_blur:no_motion_blur dof:no_dof bloom:no_bloom aa:no_aa \
                aniso:no_aniso dynamic_lights:no_dynamic_lights sun_ao:no_sun_ao \
                draw_distance:draw_distance_40; do
        [ "$(choice fx_${pair%%:*})" = 0 ] && gfx_on="$gfx_on ${pair#*:}"
    done
    case "$(choice gfx_shadows)" in
        2) gfx_on="$gfx_on shadows_2" ;;
        1) gfx_on="$gfx_on shadows_1" ;;
    esac
    for id in $gfx_on; do
        if [ -f "$data/gfx-$id.bin" ]; then set -- "$@" --extra-patches "$data/gfx-$id.bin"
        else echo "Graphics patch $id: not available for $exe"; fi
    done
    [ -n "$gfx_on" ] && echo "Graphics patches:$gfx_on"
    # Below 1080p with the movies in a corner of 1080p frames (BB_VDEC_CONTENT): the game's movie
    # textures and chroma plane at the decoder's 1920x1088 (the Bachata catalog's movie texture
    # lines, revvv991's chroma plane line; patches/graphics_patches.json movies_below_1080p).
    # Without them the movies were green and enlarged.
    if [ -n "$movie_patch" ]; then
        if [ -f "$data/gfx-movies_below_1080p.bin" ]; then set -- "$@" --extra-patches "$data/gfx-movies_below_1080p.bin"
        else echo "Movies below 1080p: no movie texture patch for $exe (graphics-2.txt)"; fi
    fi
    # Uncharted 2's aiming and turret blur read tiny render results back: copied back without
    # waiting (BB_TINY_READBACK, upstream uncharted_pc 0.3.8; ~13% slower elsewhere, so U2 only).
    # Without it everything behind Drake stayed blurred while aiming.
    case "$exe" in
        big2*) export BB_TINY_READBACK=1 ;;
        *) export BB_TINY_READBACK=0 ;;
    esac
    # BB_RENDER_CPUS: the host GPU threads (command processor, draw recording, Vulkan recorder,
    # presentation) off the slowest cores when at least four faster ones remain (the Thor: 3-7).
    # From revvv991's uncharted-android: Android put those threads on the slow cores while the
    # game's job workers held the fast ones. BB_RENDER_CPUS= (empty) leaves them anywhere.
    if [ -z "${BB_RENDER_CPUS+set}" ]; then
        slowest= fastest=0
        for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/cpuinfo_max_freq; do
            read -r khz < "$f" 2>/dev/null || continue
            [ -z "$slowest" ] || [ "$khz" -lt "$slowest" ] && slowest=$khz
            [ "$khz" -gt "$fastest" ] && fastest=$khz
        done
        # Only when the slowest are little cores (under 3/4 of the fastest clock, as revvv991's
        # uncharted-android: the Thor's run at 0.63, a Snapdragon 8 Elite's slower ones at 0.82).
        [ -n "$slowest" ] && [ $((slowest * 4)) -ge $((fastest * 3)) ] && slowest=$fastest
        fast= count=0
        for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/cpuinfo_max_freq; do
            read -r khz < "$f" 2>/dev/null || continue
            cpu=${f#/sys/devices/system/cpu/cpu}; cpu=${cpu%%/*}
            [ "$khz" -gt "$slowest" ] && { fast=${fast:+$fast,}$cpu; count=$((count + 1)); }
        done
        if [ "$count" -ge 4 ]; then export BB_RENDER_CPUS=$fast; echo "Render threads: CPUs $fast"; fi
    fi
    # BB_GAME_CPUS: the CPUs the game may run on (taskset -c list), e.g. 2-7.
    [ -n "${BB_GAME_CPUS:-}" ] && set -- /system/bin/taskset -c "$BB_GAME_CPUS" "$@"
    rm -f "$loadexec"
    "$@"
    code=$?
    if [ "$code" != 101 ] || [ ! -f "$loadexec" ]; then
        echo "Game exited with status $code"
        exit "$code"
    fi
    target=$(sed -n '1p' "$loadexec" | tr '\\' '/' | sed 's|.*/||' | tr -d '\r')
    [ -n "$target" ] || { echo "The game asked for another executable without a name."; exit 2; }
    echo "Switching to $target"
    exe=$target
    # ARM: a game the collection menu starts (as part of the collection) stayed on a grey or white
    # screen; started on its own (logos first) it runs (run.py does the same on ARM hosts).
    # BB_KEEP_COLLECTION_FLAG=1 keeps the menu's arguments.
    guest_argv="$base/data/guest-argv.txt"
    cp "$loadexec" "$guest_argv"
    if [ "$target" != eboot.bin ] && grep -q '^ispartofuccollection' "$loadexec" && [ "${BB_KEEP_COLLECTION_FLAG:-0}" != 1 ]; then
        echo "Starting $target on its own (intro logos first), not as part of the collection"
        guest_argv=
    fi
done
