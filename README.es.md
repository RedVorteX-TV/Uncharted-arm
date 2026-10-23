# Uncharted: The Nathan Drake Collection para ARM (Linux y Android)

**[Read in English](README.md)** · **[RedVorteX-TV en YouTube](https://youtube.com/@redvortex-tv)**

Uncharted 1, 2 y 3 de la colección de PS4 en consolas portátiles y móviles ARM con GPU Snapdragon:
Linux (AYN Odin 3 con Armada, una AppImage) y Android (un APK). El código x86-64 de los juegos se
ejecuta con FEXCore y sus gráficos van a Vulkan (Mesa Turnip en Adreno), sobre la misma base de port
nativo que las versiones de Linux y Windows de la colección.

**En desarrollo.** Los tres juegos arrancan y se juegan, con problemas conocidos (abajo). Es un
proyecto de fans no oficial, sin relación con Sony Interactive Entertainment ni con Naughty Dog.

## Estado

| | Linux (Odin 3, Snapdragon 8 Elite) | Android (AYN Thor, Snapdragon 8 Gen 2) |
|---|---|---|
| Uncharted 1 | Jugable | Jugable |
| Uncharted 2 | Jugable | Jugable |
| Uncharted 3 | Jugable | Jugable |

- El rendimiento lo limita más la traducción a Vulkan de cada dibujo en el procesador (unos 2.000
  por fotograma) que la GPU. Uncharted 2 va a unos 33 FPS en el modo eco de la Odin 3 a 720p; en la
  Thor, Uncharted 1 va algo por encima de 30 FPS y Uncharted 2 y 3 a 25-30, con bajadas a 26-27 en
  zonas exigentes. El objetivo en la Thor son 30 FPS estables.
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

Y sobre [FEX-Emu](https://github.com/FEX-Emu/FEX) (FEXCore), [Mesa](https://mesa3d.org) (Turnip),
[shadPS4](https://github.com/shadps4-emu/shadPS4), [FFmpeg](https://ffmpeg.org) y los proyectos que
cita el [README del fork de Linux](docs/original-readme/README.linux-fork.md). Cada commit de este
repositorio conserva a su autor.

## Licencia

GNU GPL v2 o posterior ([LICENSE](LICENSE)), como los proyectos en los que se basa. Los componentes
de terceros mantienen sus propias licencias (sus textos van con la AppImage y el APK).
