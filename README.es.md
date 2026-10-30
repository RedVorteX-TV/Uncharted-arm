# Uncharted: The Nathan Drake Collection para ARM (Linux y Android)

**[Read in English](README.md)** · **[RedVorteX-TV en YouTube](https://youtube.com/@redvortex-tv)**

Un fork de [Supermedo/uncharted_pc](https://github.com/Supermedo/uncharted_pc), a través de
[spacepepperoni/uncharted_Linux](https://github.com/spacepepperoni/uncharted_Linux), su fork para
Linux y Steam Deck (esa es la versión para la Steam Deck y Linux x86-64).

Uncharted 1, 2 y 3 de la colección de PS4 en consolas portátiles y móviles ARM con GPU Snapdragon:
Linux (AYN Odin 3 con Armada, una AppImage) y Android (un APK). El código x86-64 de los juegos se
ejecuta con FEXCore y sus gráficos van a Vulkan (Mesa Turnip en Adreno), sobre la misma base de port
nativo que las versiones de Linux y Windows de la colección.

**En desarrollo.** Los tres juegos arrancan y se juegan, con problemas conocidos (abajo). Es un
proyecto de fans no oficial, sin relación con Sony Interactive Entertainment ni con Naughty Dog.

## Estado

| | Linux (Odin 3, Snapdragon 8 Elite) | Android (AYN Thor, Snapdragon 8 Gen 2) |
|---|---|---|
| Uncharted 1 | Perfectamente jugable | Jugable |
| Uncharted 2 | Perfectamente jugable | Funciona: se ejecuta, pero a veces baja de 20 FPS |
| Uncharted 3 | Perfectamente jugable | Funciona: se ejecuta, pero a veces baja de 20 FPS |

- Perfectamente jugable: fluido y estable de principio a fin. Jugable: se puede pasar de principio
  a fin con unos FPS suficientemente estables. Funciona: se ejecuta y se puede jugar, pero los FPS
  todavía no son suficientes para considerarlo jugable.
- En la Odin 3 el límite es la traducción a Vulkan de cada dibujo en el procesador (unos 2.000 por
  fotograma): Uncharted 2 va a unos 33 FPS en modo eco a 720p. En la Thor el límite es la GPU:
  Uncharted 1 va a 26-30 FPS, y Uncharted 2 y 3 a 20-30, con bajadas por debajo de 20 en zonas
  exigentes.
- Resolución de render: 1080p, 720p (el modo 720p del propio juego en PS4) o 540p (para GPU menos
  potentes), en el menú del juego (L3 + R3, y luego "Save and restart the game") y en los launchers
  (Linux: Ajustes; Android: Gráficos). 540p le quita carga a la GPU. Con el skinning por lotes de la
  0.1.6, Uncharted 2 a 540p va a unos 25-45 FPS en la Thor, según la zona.
- Problemas conocidos: en Uncharted 2 los personajes a veces atraviesan partes del escenario; un
  cierre al final de una cinemática de Uncharted 2 se ha visto una vez.

## Qué necesitas

- **Tu propio volcado descifrado** de la colección (CUSA02320, CUSA02343, CUSA02344, ...): la carpeta
  con `eboot.bin`, `big2-ps4_Shipping.elf`, `big3-ps4_Shipping.elf`, `sce_sys`, `u1data`, `u2data` y
  `u3data`. **No se incluye ni se facilita ningún archivo del juego.**
- **Linux:** un sistema aarch64 con el driver Vulkan Turnip (el probado es Armada, basado en Fedora,
  en la AYN Odin 3) y 8 GB de RAM o más.
- **Android:** Android 9 o superior, GPU Adreno (recomendado Snapdragon 8 Gen 2 o superior) y 8 GB de
  RAM o más. El driver gráfico (Turnip) va incluido en la app.

## Instalación

**Linux (AppImage):** descarga `Uncharted-aarch64.AppImage` de las
[releases](https://github.com/RedVorteX-TV/Uncharted-arm/releases), dale permiso de ejecución y
ábrela. Elige la carpeta del juego en el launcher. El launcher se actualiza solo desde las releases
de este repositorio.

**Android (APK):** instala el APK de las
[releases](https://github.com/RedVorteX-TV/Uncharted-arm/releases), ábrelo, permite el acceso al
almacenamiento, elige la carpeta del juego y pulsa Jugar. Con "Buscar actualizaciones" activado
(sección Proyecto) el launcher avisa cuando sale una versión nueva.

## Generación de fotogramas con Lossless Scaling (LSFG)

El port puede usar la generación de fotogramas de Lossless Scaling (LSFG) a 2×, 3× o 4×. Necesita
**tu propio `Lossless.dll`**, de una copia comprada de [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/).
El DLL no se incluye ni se proporciona. Elige el archivo en el launcher (Linux: Ajustes > Lossless
Scaling (LSFG); Android: Gráficos > Lossless.dll, que lo copia dentro de la app) y después
selecciona LSFG como generación de fotogramas, ahí o en el menú del juego (L3+R3). Solo trabaja con
los fotogramas terminados, así que añade algo de latencia y puede mostrar artefactos en bordes que
se mueven rápido y en la interfaz, más cuanto más bajos sean los FPS del propio juego. En la Thor,
usa 2×.

## Frecuencia del juego (60 / 120 Hz)

Ajustes (Linux) o Gráficos (Android) > Frecuencia del juego. A 120 Hz, un fotograma que tarda algo
más de 33 ms se muestra enseguida en lugar de esperar hasta los 50 ms: tiempos de fotograma más
regulares en pantallas de 120 Hz como las de la Odin 3 y la Thor. Deja 60 Hz en una pantalla de
60 Hz.

## Compilar

- **Linux ARM** (esta rama, `main`): en el propio dispositivo, `bash packaging/armada/build.sh`
  compila dentro de un contenedor Fedora (podman o toolbox) sin instalar nada en el sistema;
  `BB_GAME_DIR=/ruta/al/volcado bash packaging/armada/run.sh` lo arranca, y
  `bash packaging/armada/appimage.sh` genera la AppImage.
- **Android** (rama `android`): `tools/android/build_bionic_deps.sh`, `build_bionic.sh`,
  `build_prepare.sh` y `build_frontend.sh` (NDK, sin Gradle); los detalles, en la cabecera de cada
  script.

Los registros están en `logs/latest.log` (Linux) y `Download/Uncharted` (Android). `BB_FRAME_STATS=1`
añade cada pocos segundos el tiempo por fotograma, las esperas a la GPU y la memoria.

## Créditos

Port a ARM y Android: **[RedVorteX-TV](https://youtube.com/@redvortex-tv)**.

Sobre el trabajo de:

- [spacepepperoni/uncharted_Linux](https://github.com/spacepepperoni/uncharted_Linux): el fork
  para Linux y Steam Deck del que parte este port (launcher, modo 720p, arreglos de Uncharted 2 y 3),
  con ngin y sus colaboradores.
- [Supermedo/uncharted_pc](https://github.com/Supermedo/uncharted_pc): el port nativo de la
  colección.
- [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) (bbport): la base de
  port nativo de PS4 sobre la que funciona.
- [MaSieS4Fun/ARM_bloodborne_pc](https://github.com/MaSieS4Fun/ARM_bloodborne_pc): la capa de
  FEXCore que ejecuta en ARM el código x86-64 de los juegos.
- [ducvd89/Bloodborne-Android](https://github.com/ducvd89/Bloodborne-Android): el runtime de
  Android y los arreglos de FSR 3.1 para Adreno y Turnip.
- [Pepin208/uncharted_pc_linux](https://github.com/Pepin208/uncharted_pc_linux): el primer intento
  en ARM del que partió este port.
- uncharted_pc-dev de Mevlock: el parche que salta el menú de la colección.
- [El port de PC de Uncharted de BilalReloaded](https://github.com/bc160201983/Uncharted-The-Nathan-Drake-Collection-PC-port)
  (GPL-2.0 o posterior), tal como Tutuzaooo lo llevó al fork de Linux: la resolución de render y
  los presets de escalado (scripts/render_res.py, el escalado de las cinemáticas en
  gpu/shim/bbport_movie.cpp).
- [revvv991/uncharted-android](https://github.com/revvv991/uncharted-android) (GPL-2.0 o
  posterior): el skinning por lotes (vk_skin_batch.cpp y sus shaders), las constantes reutilizadas,
  los hilos de GPU en los núcleos rápidos y menos despertares del hilo de envíos del juego; los
  vídeos y la escala del desenfoque por debajo de 1080p.
- [Bachata S4](https://github.com/zenithblue-oss/shadps4-arm64) (GPL-2.0 o posterior), a través de
  [erickmendes22/uncharted-android](https://github.com/erickmendes22/uncharted-android): los
  parches gráficos del launcher (patches/graphics_patches.json).
- [erickmendes22/uncharted-android](https://github.com/erickmendes22/uncharted-android)
  (GPL-2.0 o posterior): las correcciones de NaN de la cámara y de la cinemática inversa de las
  piernas en Uncharted 2 y 3, saturate convirtiendo NaN en 0, la lectura completa de los datos de
  Uncharted 2 y los resultados de sus mallas llegando a la CPU, la comprobación de la caché de
  pipelines del driver y, en Android, el presupuesto de memoria de texturas según la RAM libre y las
  pistas de rendimiento (commits de erickmendes22 en este repositorio).
- [Snapdragon Game Super Resolution 2](https://github.com/SnapdragonStudios/snapdragon-gsr) de Qualcomm
  (BSD-3-Clause): los shaders del escalador GSR 2 (gpu/shadps4/video_core/host_shaders/gsr2_*.comp).
- [StevenMXZ/Adreno-Tools-Drivers](https://github.com/StevenMXZ/Adreno-Tools-Drivers): las
  versiones de Turnip Gen8 que incluye la app de Android (V37, y V38 parcheada para Adreno 8xx).
- [Eden](https://git.eden-emu.dev/eden-emu/eden) (PR #4263) y
  [ARMSX2](https://github.com/ARMSX2/ARMSX2): los pases de generación de fotogramas LSFG, a partir
  de los de [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk)
  ([gpu/shadps4/video_core/renderer_vulkan/lsfg](gpu/shadps4/video_core/renderer_vulkan/lsfg/README.md)).

Y sobre [FEX-Emu](https://github.com/FEX-Emu/FEX) (FEXCore), [Mesa](https://mesa3d.org) (Turnip),
[shadPS4](https://github.com/shadps4-emu/shadPS4), [FFmpeg](https://ffmpeg.org) y los proyectos que
cita el [README del fork de Linux](docs/original-readme/README.linux-fork.md). Cada commit de este
repositorio conserva a su autor.

## Licencia

El código fuente está bajo la GNU GPL v2 o posterior ([LICENSE](LICENSE)), como los proyectos en
los que se basa. Los archivos de la generación de fotogramas LSFG son GPL-3.0 o posterior, así que
los binarios, que los incluyen, se distribuyen en conjunto bajo la GNU GPL v3 o posterior
([COPYING.GPL-3](COPYING.GPL-3)). Los componentes de terceros mantienen sus propias licencias (sus
textos van con la AppImage y el APK).
