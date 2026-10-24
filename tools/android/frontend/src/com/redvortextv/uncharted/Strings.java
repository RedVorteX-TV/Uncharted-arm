package com.redvortextv.uncharted;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/** The app's texts in English and Spanish (the Linux launcher's translations, launcher/uncharted_i18n.py).
 *  The language is "auto" (the system's), "en" or "es". */
final class Strings {
    private static final Map<String, String[]> TEXT = new HashMap<>();
    private static boolean spanish;

    private static void put(String key, String en, String es) { TEXT.put(key, new String[]{en, es}); }

    static {
        put("subtitle", "The Nathan Drake Collection", "The Nathan Drake Collection");
        put("credit", "ARM port by RedVorteX-TV", "Port ARM por RedVorteX-TV");
        put("game", "Game", "Juego");
        put("perf_stats", "Performance statistics in the log", "Estadísticas de rendimiento en el registro");
        put("perf_stats_hint", "Frame rate, threads, GPU waits and memory every few seconds (BB_FRAME_STATS, "
                + "BB_THREAD_STATS). For reports; leave it off to play.",
            "FPS, hilos, esperas de la GPU y memoria cada pocos segundos (BB_FRAME_STATS, BB_THREAD_STATS). "
                + "Para informes; déjalo apagado para jugar.");
        put("logs", "Game log", "Registro del juego");
        put("logs_hint", "Every game run writes its log here (send it when something goes wrong):",
            "Cada partida guarda su registro aquí (envíalo si algo falla):");
        put("no_log", "No log yet: start a game first.", "Aún no hay registro: inicia un juego primero.");
        put("collection", "Collection menu", "Menú de la colección");
        put("collection_detail", "Uncharted 1, 2 and 3", "Uncharted 1, 2 y 3");
        put("u2", "Uncharted 2", "Uncharted 2");
        put("u2_detail", "Among Thieves", "El reino de los ladrones");
        put("u3", "Uncharted 3", "Uncharted 3");
        put("u3_detail", "Drake's Deception", "La traición de Drake");
        put("play", "Play", "Jugar");
        put("graphics", "Graphics", "Gráficos");
        put("fsr1", "FSR 1 (AMD FidelityFX, sharp)", "FSR 1 (AMD FidelityFX, nítido)");
        put("fsr1_hint", "FSR 1 keeps edges sharp when the image is enlarged; bilinear is softer.",
            "FSR 1 mantiene los bordes nítidos al ampliar la imagen; el bilineal es más suave.");
        put("game_rate", "Game display rate", "Frecuencia del juego");
        put("game_rate_120", "120 Hz (120 Hz screens)", "120 Hz (pantallas de 120 Hz)");
        put("game_rate_hint", "The refresh rate the game paces its frames to. 120 Hz: steadier frame times on 120 Hz "
                + "screens such as the Thor's (a frame a little over 33 ms is not held until 50 ms). Keep 60 Hz on a "
                + "60 Hz screen.",
            "La frecuencia a la que el juego reparte sus fotogramas. 120 Hz: tiempos de fotograma más regulares en "
                + "pantallas de 120 Hz como la de la Thor (un fotograma de algo más de 33 ms no espera hasta los 50 ms). "
                + "Deja 60 Hz en una pantalla de 60 Hz.");
        put("render_res", "Render resolution", "Resolución de render");
        put("render_res_hint", "The size the whole game draws at, scaled to the screen with FSR 1. 720p: the game's "
                + "own PS4 720p mode, much faster than 1080p. 540p: for GPUs that 720p is still too much for "
                + "(softer picture). Also in the in-game menu (L3 + R3).",
            "El tamaño al que se dibuja todo el juego, escalado a la pantalla con FSR 1. 720p: el modo 720p del "
                + "propio juego en PS4, mucho más rápido que 1080p. 540p: para GPU a las que 720p aún les cuesta "
                + "(imagen más suave). También en el menú del juego (L3 + R3).");
        put("frame_gen", "Frame generation", "Generación de fotogramas");
        put("frame_gen_hint", "Frames made between two of the game's, for smoother motion, with a little more "
                + "input latency: LSFG, Lossless Scaling's, with your own Lossless.dll (below). Also in the in-game "
                + "menu (L3 + R3).",
            "Fotogramas creados entre dos del juego, para un movimiento más fluido, con algo más de latencia: LSFG, "
                + "el de Lossless Scaling, con tu propio Lossless.dll (abajo). También en el menú del juego "
                + "(L3 + R3).");
        put("frame_gen_off", "Off", "Desactivada");
        put("frame_gen_multiplier", "Frame generation multiplier", "Multiplicador de la generación");
        put("frame_gen_multiplier_hint", "Frames shown per game frame. On the Thor 2× works best: 3× and 4× "
                + "lower the game's own frame rate.",
            "Fotogramas mostrados por cada fotograma del juego. En la Thor lo mejor es 2×: 3× y 4× bajan los FPS "
                + "del propio juego.");
        put("lsfg_dll", "Lossless.dll (LSFG)", "Lossless.dll (LSFG)");
        put("lsfg_dll_hint", "Choose Lossless.dll from your own copy of Lossless Scaling. It is not included "
                + "with the app.",
            "Elige el Lossless.dll de tu propia copia de Lossless Scaling. No se incluye con la app.");
        put("lsfg_dll_chosen", "Lossless.dll copied into the app. Choose it again to replace it.",
            "Lossless.dll copiado en la app. Elígelo otra vez para sustituirlo.");
        put("lsfg_not_dll", "That file is not a DLL: choose Lossless.dll.",
            "Ese archivo no es un DLL: elige Lossless.dll.");
        put("frame_rate", "Frame rate", "Fotogramas por segundo");
        put("frame_rate_hint", "A frame rate limit: 40 FPS paces evenly on 120 Hz screens. Also in the in-game menu.",
            "Un límite de fotogramas: 40 FPS va fluido en pantallas de 120 Hz. También en el menú del juego.");
        put("fps_30", "30 FPS", "30 FPS");
        put("fps_40", "40 FPS (recommended)", "40 FPS (recomendado)");
        put("fps_45", "45 FPS", "45 FPS");
        put("fps_60", "60 FPS", "60 FPS");
        put("fps_0", "No limit", "Sin límite");
        put("compatibility", "Compatibility (ARM)", "Compatibilidad (ARM)");
        put("arm_uma_readback", "Fast GPU memory reads (Uncharted 2 and 3)",
            "Lecturas rápidas de la memoria de la GPU (Uncharted 2 y 3)");
        put("arm_uma_readback_hint", "Turn off if, in Uncharted 2 or 3, characters float or slide over the ground, "
                + "explosions or vehicle damage do not behave as they should, or surfaces flash in flat colours (cyan, "
                + "white). With it off the game waits for the GPU at those reads: slower, as in the original port.",
            "Desactívalo si, en Uncharted 2 o 3, los personajes flotan o se deslizan por el suelo, las explosiones o los "
                + "daños de los vehículos no se comportan como deberían, o algunas superficies parpadean con colores "
                + "planos (cian, blanco). Desactivado, el juego espera a la GPU en esas lecturas: más lento, como en el "
                + "port original.");
        put("arm_flush_draws", "Send GPU work in small batches (Uncharted 2 and 3)",
            "Enviar el trabajo a la GPU en tandas pequeñas (Uncharted 2 y 3)");
        put("arm_flush_draws_hint", "Turn off if a scene freezes (the picture stops while the sound goes on) or the "
                + "game hangs at the same spot every time. With it off Uncharted 2 and 3 run at a somewhat lower frame rate.",
            "Desactívalo si una escena se congela (la imagen se para mientras el sonido sigue) o el juego se cuelga "
                + "siempre en el mismo sitio. Desactivado, Uncharted 2 y 3 van con algo menos de FPS.");
        put("arm_job_spin", "Let idle game threads sleep", "Dejar que los hilos inactivos del juego duerman");
        put("arm_job_spin_hint", "Turn off if the game stutters or pauses for a moment again and again in one place, "
                + "or loading screens take much longer than usual. With it off the game uses about twice the CPU: more "
                + "battery and heat.",
            "Desactívalo si el juego da tirones o se detiene un momento una y otra vez en un mismo sitio, o si las "
                + "pantallas de carga tardan mucho más de lo normal. Desactivado, el juego usa más o menos el doble de "
                + "CPU: más batería y más calor.");
        put("recommended", "Recommended for ARM handhelds", "Recomendado para portátiles ARM");
        put("recommended_hint", "720p with FSR 1, a 40 FPS limit and all the compatibility fixes on.",
            "720p con FSR 1, límite de 40 FPS y todos los arreglos de compatibilidad activados.");
        put("recommended_done", "Recommended settings applied and saved.", "Ajustes recomendados aplicados y guardados.");
        put("system", "System", "Sistema");
        put("gpu_driver", "GPU driver", "Driver gráfico");
        put("driver_auto", "Automatic (recommended)", "Automático (recomendado)");
        put("driver_system", "Phone's own driver", "Driver propio del dispositivo");
        put("driver_add", "Add driver…", "Añadir driver…");
        put("driver_use", "Use", "Usar");
        put("driver_delete", "Delete", "Borrar");
        put("game_folder", "Game folder", "Carpeta del juego");
        put("not_chosen", "Not chosen", "Sin elegir");
        put("folder_title", "Uncharted game folder", "Carpeta del juego Uncharted");
        put("folder_message", "Select the extracted game folder (e.g. CUSA02344, the folder with eboot.bin and "
                + "big2-ps4_Shipping.elf) in your device's storage.",
            "Elige la carpeta del juego extraído (por ejemplo CUSA02344, la que tiene eboot.bin y "
                + "big2-ps4_Shipping.elf) en el almacenamiento del dispositivo.");
        put("choose_folder", "Choose folder", "Elegir carpeta");
        put("not_a_game", "That folder has no eboot.bin.", "Esa carpeta no tiene eboot.bin.");
        put("choose_again", "Choose again", "Elegir otra");
        put("cancel", "Cancel", "Cancelar");
        put("storage_needed", "Uncharted needs storage access to read the game folder.",
            "Uncharted necesita acceso al almacenamiento para leer la carpeta del juego.");
        put("touch_controls", "On-screen controller", "Mando en pantalla");
        put("touch_controls_hint", "Touch buttons over the game, for playing without a controller.",
            "Botones táctiles sobre el juego, para jugar sin mando.");
        put("edit_layout", "Edit controller layout", "Editar el mando en pantalla");
        put("language", "Language", "Idioma");
        put("lang_auto", "Automatic (system language)", "Automático (idioma del sistema)");
        put("controls_hint", "Controller: D-pad to move, A to choose, left/right to change a value, B to go back. "
                + "In the game, L3 + R3 opens its menu and the Back/Select button with Start opens this app's menu.",
            "Mando: cruceta para moverte, A para elegir, izquierda/derecha para cambiar un valor y B para volver. "
                + "En el juego, L3 + R3 abre su menú y Back/Select con Start abre el menú de la app.");
        put("installing_runtime", "Installing the runtime…", "Instalando el runtime…");
        put("starting", "Starting Uncharted…", "Iniciando Uncharted…");
        put("preparing", "Preparing the game (the first start takes a while)…",
            "Preparando el juego (la primera vez tarda un poco)…");
        put("game_stopped", "The game stopped (%s).", "El juego se ha cerrado (%s).");
        put("driver_trouble", " It looks like a GPU driver problem: try another driver in System → GPU driver.",
            " Parece un problema del driver gráfico: prueba otro en Sistema → Driver gráfico.");
        put("back_to_game", "Back to game", "Volver al juego");
        put("pause", "Pause game", "Pausar el juego");
        put("resume", "Play game", "Reanudar el juego");
        put("paused_chip", "Paused · tap to play", "En pausa · toca para seguir");
        put("restart", "Restart game", "Reiniciar el juego");
        put("restart_question", "Restart the game? Unsaved progress is lost.",
            "¿Reiniciar el juego? Se pierde el progreso no guardado.");
        put("quit_to_launcher", "Quit to launcher", "Salir al launcher");
        put("quit_question", "Quit the game? Unsaved progress is lost.",
            "¿Salir del juego? Se pierde el progreso no guardado.");
        put("yes", "Yes", "Sí");
        put("no", "No", "No");
        put("menu_hint", "Swipe left or press B to return", "Desliza a la izquierda o pulsa B para volver");
        put("controls", "Controls", "Controles");
        put("on", "On", "Sí");
        put("off", "Off", "No");
        put("project", "Project", "Proyecto");
        put("youtube", "YouTube channel", "Canal de YouTube");
        put("youtube_hint", "RedVorteX-TV: videos about this port, handhelds and emulation.",
            "RedVorteX-TV: vídeos de este port, consolas portátiles y emulación.");
        put("github", "GitHub", "GitHub");
        put("github_hint", "The port's code, news and downloads; report problems there.",
            "El código del port, novedades y descargas; los problemas se reportan allí.");
        put("update_check", "Check for updates", "Buscar actualizaciones");
        put("update_check_hint", "When the app opens, asks GitHub whether there is a newer version. Nothing is "
                + "downloaded or sent.",
            "Al abrir la app, pregunta a GitHub si hay una versión nueva. No se descarga ni se envía nada.");
        put("update_available", "Version %s is available", "Versión %s disponible");
        put("update_available_hint", "You have %s. Tap to open its download page on GitHub.",
            "Tienes la %s. Toca para abrir su página de descarga en GitHub.");
        put("credits", "Credits", "Créditos");
        put("credits_text", "ARM and Android port: RedVorteX-TV. Android runtime: ducvd89 (Bloodborne-Android). Linux port: "
                + "spacepepperoni (uncharted_Linux), Supermedo (uncharted_pc), deadinside28 (bloodborne_pc). ARM layer: "
                + "MaSieS4Fun (FEXCore). Drivers: Mesa Turnip.",
            "Port ARM y Android: RedVorteX-TV. Runtime de Android: ducvd89 (Bloodborne-Android). Port de Linux: "
                + "spacepepperoni (uncharted_Linux), Supermedo (uncharted_pc), deadinside28 (bloodborne_pc). Capa ARM: "
                + "MaSieS4Fun (FEXCore). Drivers: Mesa Turnip.");
        put("restart_driver", "The game uses %s after a restart. Restart now?",
            "El juego usará %s al reiniciar. ¿Reiniciar ahora?");
        put("driver_installed", "%s is installed. Use it now?", "%s está instalado. ¿Usarlo ahora?");
        put("driver_not_installed", "Driver not installed", "Driver no instalado");
        put("driver_download", "Download from GitHub", "Descargar de GitHub");
        put("driver_zip", "Install a zip from this device", "Instalar un zip de este dispositivo");
    }

    static void setLanguage(String language) {
        if ("es".equals(language)) spanish = true;
        else if ("en".equals(language)) spanish = false;
        else spanish = "es".equals(Locale.getDefault().getLanguage());
    }

    static String get(String key) {
        String[] t = TEXT.get(key);
        if (t == null) return key;
        return spanish ? t[1] : t[0];
    }

    static String format(String key, Object... args) { return String.format(get(key), args); }

    private Strings() {}
}
