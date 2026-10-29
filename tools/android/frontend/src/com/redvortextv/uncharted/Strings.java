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
        put("game_rate_auto", "Automatic (fast)", "Automático (rápido)");
        put("game_rate_hint", "The refresh rate the game paces its frames to. 120 Hz: steadier frame times on 120 Hz "
                + "screens (a frame a little over 33 ms is not held until 50 ms). Automatic: a frame is shown as soon "
                + "as it is ready, up to the screen's rate (at most 120 FPS); for 60 FPS and above. Keep 60 Hz on a "
                + "60 Hz screen.",
            "La frecuencia a la que el juego reparte sus fotogramas. 120 Hz: tiempos de fotograma más regulares en "
                + "pantallas de 120 Hz (un fotograma de algo más de 33 ms no espera hasta los 50 ms). Automático: cada "
                + "fotograma se muestra en cuanto está listo, hasta la frecuencia de la pantalla (120 FPS como mucho); "
                + "para 60 FPS o más. Deja 60 Hz en una pantalla de 60 Hz.");
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
        put("frame_gen_multiplier_hint", "Frames shown per game frame. 2× usually works best: 3× and 4× "
                + "lower the game's own frame rate, more so on slower GPUs.",
            "Fotogramas mostrados por cada fotograma del juego. Normalmente lo mejor es 2×: 3× y 4× bajan los FPS "
                + "del propio juego, más cuanto menos potente sea la GPU.");
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
        put("profile", "Profile", "Perfil");
        put("profile_hint", "A recommended set of the settings below. Change any of them and it becomes Custom.",
            "Un conjunto recomendado de los ajustes de abajo. Si cambias alguno, pasa a Personalizado.");
        put("profile_note", "Custom: the settings below as you chose them.", "Personalizado: los ajustes de abajo tal como los elegiste.");
        put("profile_custom", "Custom", "Personalizado");
        put("profile_low", "Low", "Bajo");
        put("profile_medium", "Medium", "Medio");
        put("profile_high", "High", "Alto");
        put("profile_high_perf", "High performance", "Alto rendimiento");
        put("profile_battery", "High autonomy", "Alta autonomía");
        put("profile_low_desc", "960 × 540 with FSR 1, every performance patch, 30 FPS. The steadiest on mid-range and lower devices.",
            "960 × 540 con FSR 1, todos los parches de rendimiento, 30 FPS. Lo más estable en gama media y baja.");
        put("profile_medium_desc", "960 × 540 with FSR 1, the game's own effects, 30 FPS.",
            "960 × 540 con FSR 1, los efectos originales del juego, 30 FPS.");
        put("profile_high_desc", "1280 × 720 upscaled to 1080p with GSR 2, the game's own effects, 60 FPS. For high-end devices.",
            "1280 × 720 escalado a 1080p con GSR 2, los efectos originales del juego, 60 FPS. Para gama alta.");
        put("profile_high_perf_desc", "For powerful devices: 1280 × 720 upscaled with GSR 2, the game's own effects and no frame limit. "
                + "Frame pacing may vary more than with a limit.",
            "Para dispositivos potentes: 1280 × 720 escalado con GSR 2, los efectos originales del juego y sin límite de FPS. "
                + "La fluidez puede variar más que con un límite.");
        put("profile_battery_desc", "For powerful devices, to save battery: 1280 × 720 with FSR 1, the game's own effects, 40 FPS. "
                + "Needs a 120 Hz screen (on 60 Hz it uses 30 FPS).",
            "Para dispositivos potentes, ahorrando batería: 1280 × 720 con FSR 1, los efectos originales del juego, 40 FPS. "
                + "Requiere pantalla de 120 Hz (a 60 Hz usa 30 FPS).");
        put("profile_done", "Profile %s applied and saved.", "Perfil %s aplicado y guardado.");
        put("profile_battery_60hz", "High autonomy applied with 30 FPS: 40 FPS needs a 120 Hz screen and this one goes up to 60 Hz.",
            "Alta autonomía aplicada a 30 FPS: los 40 FPS requieren pantalla de 120 Hz y esta llega a 60 Hz.");
        put("fps_40", "40 FPS", "40 FPS");
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
        put("arm_skin_batch", "Batched character skinning", "Skinning de personajes por lotes");
        put("arm_skin_batch_hint", "Deforms the characters' meshes in one GPU job instead of ~1300 a frame, with "
                + "the two-stage draw pipeline: much faster on high-end devices. On low and mid-range devices it can run "
                + "slower: try the game with it on and off and keep the faster one. Turn off as well if characters "
                + "stretch, flicker or lose parts.",
            "Deforma las mallas de los personajes en un solo trabajo de GPU en vez de ~1300 por fotograma, con el "
                + "doble carril de dibujo: mucho más rápido en dispositivos de gama alta. En dispositivos de gama baja o media puede "
                + "ir peor: prueba el juego activado y desactivado y quédate con lo que vaya mejor. Desactívalo "
                + "también si los personajes se estiran, parpadean o pierden partes.");
        put("arm_job_spin", "Let idle game threads sleep", "Dejar que los hilos inactivos del juego duerman");
        put("arm_job_spin_hint", "Turn off if the game stutters or pauses for a moment again and again in one place, "
                + "or loading screens take much longer than usual. With it off the game uses about twice the CPU: more "
                + "battery and heat.",
            "Desactívalo si el juego da tirones o se detiene un momento una y otra vez en un mismo sitio, o si las "
                + "pantallas de carga tardan mucho más de lo normal. Desactivado, el juego usa más o menos el doble de "
                + "CPU: más batería y más calor.");
        put("adpf", "Android performance hints", "Pistas de rendimiento de Android");
        put("adpf_hint", "Tells Android which threads draw the frames. Efficiency (Android 15+) does not let it raise "
                + "the clocks to the top when a frame is late: less heat for the same frame rate. Performance raises them "
                + "(the High performance profile). Turn it off if it clashes with a CPU frequency tool.",
            "Le dice a Android qué hilos dibujan los fotogramas. Eficiencia (Android 15+) no deja que suba las "
                + "frecuencias al máximo cuando un fotograma llega tarde: menos calor con los mismos FPS. Rendimiento sí "
                + "las sube (el perfil Alto rendimiento). Desactívalo si choca con una herramienta de frecuencias de CPU.");
        put("adpf_efficiency", "Efficiency", "Eficiencia");
        put("adpf_performance", "Performance", "Rendimiento");
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
        put("saves_export", "Export saves", "Exportar partidas");
        put("saves_export_hint", "Saves the three games' saves to a .zip file of your choice (a backup, or to move "
                + "them to another device or to the Linux version).", "Guarda las partidas de los tres juegos en un "
                + "archivo .zip donde elijas (una copia de seguridad, o para pasarlas a otro dispositivo o a la versión "
                + "de Linux).");
        put("saves_import", "Import saves", "Importar partidas");
        put("saves_import_hint", "Replaces the saves with those of an exported .zip (from this app or the Linux "
                + "version's savedata folder). The current ones are kept aside in the app's folder.",
            "Sustituye las partidas por las de un .zip exportado (de esta app o de la carpeta savedata de la versión "
                + "de Linux). Las actuales se guardan aparte en la carpeta de la app.");
        put("saves_exported", "Saves exported", "Partidas exportadas");
        put("saves_imported", "Saves imported", "Partidas importadas");
        put("saves_none", "There are no saves yet", "Todavía no hay partidas guardadas");
        put("saves_not_zip", "That file has no saves (a zip with a savedata folder)",
            "Ese archivo no tiene partidas (un zip con una carpeta savedata)");
        put("gfx", "Graphics patches", "Parches gráficos");
        put("gfx_note", "Optional patches from the Bachata S4 catalog, all off: the game as it is. One switched on "
                + "changes the game's own graphics settings (versions 1.00 and 1.02, checked against your executable "
                + "before it is applied): less GPU work, a plainer picture. Measure the gain on your device before "
                + "keeping it. Applied at the next start.",
            "Parches opcionales del catálogo de Bachata S4, todos desactivados: el juego tal como es. Al activar uno se "
                + "cambian los ajustes gráficos del propio juego (versiones 1.00 y 1.02, comprobado con tu ejecutable "
                + "antes de aplicarlo): menos trabajo para la GPU, una imagen más sencilla. Mide la mejora en tu "
                + "dispositivo antes de dejarlo. Se aplica al siguiente arranque.");
        put("gfx_shadows", "Shadow cascades", "Cascadas de sombras");
        put("gfx_shadows_hint", "Fewer shadow maps: less GPU work, shadows end closer.",
            "Menos mapas de sombras: menos trabajo de GPU, las sombras acaban más cerca.");
        put("gfx_shadows_0", "Original", "Original");
        put("gfx_shadows_2", "2", "2");
        put("gfx_shadows_1", "1", "1");
        put("gfx_skip_logos", "Skip startup logos (Uncharted 1 and 2)", "Saltar los logos de inicio (Uncharted 1 y 2)");
        put("gfx_skip_logos_hint", "Not available for Uncharted 3.", "No disponible para Uncharted 3.");
        put("gfx_no_water_reflections", "No water reflections", "Sin reflejos del agua");
        put("gfx_no_ssao", "No ambient occlusion (SSAO)", "Sin oclusión ambiental (SSAO)");
        put("gfx_no_fog", "No fog", "Sin niebla");
        put("gfx_no_volumetric", "No volumetric lights", "Sin luces volumétricas");
        put("gfx_no_motion_blur", "No motion blur", "Sin desenfoque de movimiento");
        put("gfx_no_motion_blur_hint", "Not applied with FSR 3.1, GSR 2 or TAA: they take the camera's motion from "
                + "the motion blur's pass.",
            "No se aplica con FSR 3.1, GSR 2 ni TAA: toman el movimiento de la cámara del pase del desenfoque.");
        put("gfx_no_dof", "No depth of field", "Sin profundidad de campo");
        put("gfx_no_bloom", "No bloom", "Sin resplandor (bloom)");
        put("gfx_no_aa", "No game anti-aliasing", "Sin el antialiasing del juego");
        put("gfx_no_aa_hint", "Not applied with FSR 3.1, GSR 2 or TAA: they start from the game's final frame copy, "
                + "which this removes, and they are the anti-aliasing then.",
            "No se aplica con FSR 3.1, GSR 2 ni TAA: parten de la copia final del fotograma del juego, que esto quita, "
                + "y ya hacen ellos de antialiasing.");
        put("gfx_no_aniso", "No anisotropic filtering", "Sin filtrado anisotrópico");
        put("gfx_no_dynamic_lights", "No dynamic lights", "Sin luces dinámicas");
        put("gfx_no_sun_ao", "No sunlight ambient occlusion", "Sin oclusión ambiental del sol");
        put("gfx_draw_distance_40", "Draw distance 40 m", "Distancia de dibujado 40 m");
        put("gfx_draw_distance_40_hint", "Extreme: distant scenery disappears.",
            "Extremo: el escenario lejano desaparece.");
        put("gfx_off", "Turn all off (the game as it is)", "Desactivar todos (el juego tal como es)");
        put("upscaling", "Upscaling", "Escalado");
        put("upscaling_hint", "FSR 1 enlarges the frame alone: sharp, no anti-aliasing, almost free. FSR 3.1 and "
                + "GSR 2 (Snapdragon Game Super Resolution, made for Adreno GPUs) rebuild each frame with the game's "
                + "depth and motion: they smooth edges and, with a quality below Native, draw fewer pixels and "
                + "rebuild 1920 × 1080. GSR 2 costs less GPU time than FSR 3.1.",
            "FSR 1 solo amplía la imagen: nítida, sin antialiasing, casi gratis. FSR 3.1 y GSR 2 (Snapdragon Game "
                + "Super Resolution, hecho para GPU Adreno) reconstruyen cada fotograma con la profundidad y el "
                + "movimiento del juego: suavizan los bordes y, con una calidad por debajo de Nativa, dibujan menos "
                + "píxeles y reconstruyen 1920 × 1080. GSR 2 gasta menos GPU que FSR 3.1.");
        put("upscaling_off", "Off (bilinear)", "Desactivado (bilineal)");
        put("upscaling_taa", "TAA (anti-aliasing only)", "TAA (solo antialiasing)");
        put("quality", "Quality (FSR 3.1 and GSR 2)", "Calidad (FSR 3.1 y GSR 2)");
        put("quality_hint", "Native: only anti-aliasing, at the render resolution above. The others set the render "
                + "size themselves and upscale it to 1920 × 1080, with the HUD and menus at 1920 × 1080: Quality "
                + "1280 × 720, Balanced 1136 × 636, Performance 960 × 540, Ultra Performance 640 × 360. Then the "
                + "render resolution above is not used.",
            "Nativa: solo antialiasing, a la resolución de render de arriba. Las demás eligen ellas mismas el tamaño "
                + "de render y lo escalan a 1920 × 1080, con el HUD y los menús a 1920 × 1080: Calidad 1280 × 720, "
                + "Equilibrado 1136 × 636, Rendimiento 960 × 540, Ultra rendimiento 640 × 360. Entonces la "
                + "resolución de render de arriba no se usa.");
        put("quality_0", "Native", "Nativa");
        put("quality_1", "Quality", "Calidad");
        put("quality_2", "Balanced", "Equilibrado");
        put("quality_3", "Performance", "Rendimiento");
        put("quality_4", "Ultra Performance", "Ultra rendimiento");
        put("touch_style", "On-screen controller style", "Estilo del mando en pantalla");
        put("touch_style_hint", "Classic: the PlayStation colours on the face buttons. PS5: white symbols, as on a "
                + "DualSense.", "Clásico: los colores de PlayStation en los botones de acción. PS5: símbolos blancos, "
                + "como en un DualSense.");
        put("touch_style_classic", "Classic", "Clásico");
        put("touch_style_ps5", "PS5", "PS5");
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

    static boolean has(String key) { return TEXT.containsKey(key); }

    static String get(String key) {
        String[] t = TEXT.get(key);
        if (t == null) return key;
        return spanish ? t[1] : t[0];
    }

    static String format(String key, Object... args) { return String.format(get(key), args); }

    private Strings() {}
}
