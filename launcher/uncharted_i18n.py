# SPDX-License-Identifier: GPL-2.0-or-later
"""The launcher's languages (ARM port): English, the texts in the code, and Spanish.

`tr` gives a text in the chosen language; texts without a translation stay in English. The
choice (ui_language: auto, en, es) is made before the window is built; auto follows the system
language (LC_ALL, LC_MESSAGES, LANG, LANGUAGE)."""
import os

LANGUAGES = [('auto', 'Automatic (system language)'), ('en', 'English'), ('es', 'Español')]
_current = 'en'


def resolve(choice):
    """'en' or 'es' for a ui_language setting."""
    if choice in ('en', 'es'):
        return choice
    for name in ('LC_ALL', 'LC_MESSAGES', 'LANG', 'LANGUAGE'):
        value = os.environ.get(name, '')
        if value and value not in ('C', 'POSIX') and not value.startswith('C.'):
            return 'es' if value.lower().startswith('es') else 'en'
    return 'en'


def set_language(code):
    global _current
    _current = code if code in ('en', 'es') else 'en'


def current():
    return _current


def tr(text):
    if _current == 'es':
        return ES.get(text, text)
    return text


ES = {
    # ---- pages, navigation, buttons ----
    'Library': 'Biblioteca',
    'Settings': 'Ajustes',
    'Controls': 'Controles',
    'Files': 'Archivos',
    'Advanced': 'Avanzado',
    'Log': 'Registro',
    'Game': 'Juego',
    'Play': 'Jugar',
    'Stop': 'Detener',
    'Update': 'Actualizar',
    'Open': 'Abrir',
    'Browse…': 'Examinar…',
    'Copy': 'Copiar',
    'Add to Steam': 'Añadir a Steam',
    'Desktop shortcut': 'Acceso directo',
    'Save changes': 'Guardar cambios',
    'Settings saved': 'Ajustes guardados',
    'Default keys': 'Teclas por defecto',
    'Default controller buttons': 'Botones del mando por defecto',
    'Clear shader cache': 'Borrar la caché de shaders',
    'Open the saves folder': 'Abrir la carpeta de partidas',
    'Check for updates': 'Buscar actualizaciones',
    'Releases page': 'Página de versiones',
    "What's new": 'Novedades',
    'Create bug report': 'Crear informe de error',
    'Show the game log': 'Ver el registro del juego',
    'Open the logs folder': 'Abrir la carpeta de registros',
    'Checking…': 'Comprobando…',
    'Ready': 'Listo',
    'version': 'versión',
    'version {}': 'versión {}',
    'by {}': 'por {}',
    'Linux port by spacepepperoni': 'Port a Linux por spacepepperoni',
    'ARM port by RedVorteX-TV': 'Port a ARM por RedVorteX-TV',
    'YouTube channel: RedVorteX-TV': 'Canal de YouTube: RedVorteX-TV',
    'Lossless Scaling LSFG (your Lossless.dll)': 'Lossless Scaling LSFG (tu Lossless.dll)',
    '3× (LSFG only)': '3× (solo LSFG)',
    '4× (LSFG only)': '4× (solo LSFG)',
    'FSR 3 or Lossless Scaling\'s LSFG, which uses your own Lossless.dll (chosen below). Smoother motion, a '
    'little more input latency.':
        'FSR 3 o LSFG de Lossless Scaling, que usa tu propio Lossless.dll (elígelo abajo). Movimiento más fluido, '
        'algo más de latencia.',
    'Lossless Scaling (LSFG)': 'Lossless Scaling (LSFG)',
    'Lossless.dll from your copy of Lossless Scaling (Steam: right-click it, Manage, Browse local files). It is '
    'not included with the port. Then choose LSFG as the frame generation, here or in the in-game menu.':
        'El Lossless.dll de tu copia de Lossless Scaling (Steam: clic derecho, Administrar, Ver archivos locales). '
        'No se incluye con el port. Después elige LSFG como generación de fotogramas, aquí o en el menú del juego.',
    'All files': 'Todos los archivos',
    'Double-click a cover to launch': 'Doble clic en una portada para jugar',
    'Press a key…': 'Pulsa una tecla…',

    # ---- page titles and subtitles ----
    'Display, graphics and the console.': 'Pantalla, gráficos y consola.',
    'Options for testing, logs and bug reports.': 'Opciones de prueba, registros e informes de errores.',
    'Maintenance and options for testing.': 'Mantenimiento y opciones de prueba.',
    'Keyboard, mouse and controller. They work together; changes apply when the game starts.':
        'Teclado, ratón y mando. Funcionan a la vez; los cambios se aplican al iniciar el juego.',
    'Your own decrypted dump of the collection (CUSA02343, CUSA02320, …).':
        'Tu propia copia desencriptada de la colección (CUSA02343, CUSA02320, …).',
    'Native port: the original PS4 game code runs directly on Windows.':
        'Port nativo: el código original del juego de PS4 se ejecuta directamente en Windows.',
    'Native port: the original PS4 game code runs directly on Linux.':
        'Port nativo: el código original del juego de PS4 se ejecuta directamente en Linux.',

    # ---- library and files ----
    'Collection menu (Uncharted 1, 2 and 3)': 'Menú de la colección (Uncharted 1, 2 y 3)',
    'Opens the collection menu: choose any of the three games there.':
        'Abre el menú de la colección: elige ahí cualquiera de los tres juegos.',
    'Set your game folder on the Game page': 'Elige la carpeta del juego en la página Juego',
    'Choose the game folder on the Files page': 'Elige la carpeta del juego en la página Archivos',
    'Choose the game folder (the one with eboot.bin) on the Game page.':
        'Elige la carpeta del juego (la que tiene eboot.bin) en la página Juego.',
    'Choose the game folder with eboot.bin.': 'Elige la carpeta del juego, la que tiene eboot.bin.',
    'Choose the game folder with eboot.bin on the Files page first.':
        'Primero elige la carpeta del juego, la que tiene eboot.bin, en la página Archivos.',
    'This game is not in the chosen folder.': 'Este juego no está en la carpeta elegida.',
    '{} is not in the game folder, so this game cannot start.':
        '{} no está en la carpeta del juego, así que este juego no puede arrancar.',
    '{} is not in the game folder': '{} no está en la carpeta del juego',
    'Not in this folder: ': 'No está en esta carpeta: ',
    'No game folder yet.': 'Todavía no hay carpeta del juego.',
    'Ready: {}': 'Listo: {}',
    'Game folder': 'Carpeta del juego',
    'The folder with eboot.bin': 'La carpeta que tiene eboot.bin',
    'The folder with eboot.bin, big2-ps4_Shipping.elf and big3-ps4_Shipping.elf.':
        'La carpeta que tiene eboot.bin, big2-ps4_Shipping.elf y big3-ps4_Shipping.elf.',
    'The folder with eboot.bin, big2-ps4_Shipping.elf and big3-ps4_Shipping.elf. Leave it empty if every game has a folder of its own, below.':
        'La carpeta que tiene eboot.bin, big2-ps4_Shipping.elf y big3-ps4_Shipping.elf. Déjala vacía si cada juego tiene su propia carpeta, más abajo.',
    'Saves folder': 'Carpeta de partidas',
    'Saves': 'Partidas',
    'Where saves and the shader cache are kept': 'Donde se guardan las partidas y la caché de shaders',
    'Empty: the user folder next to the launcher.': 'Vacía: la carpeta user junto al launcher.',
    'Empty: a Saves folder in the game folder.': 'Vacía: una carpeta Saves dentro de la carpeta del juego.',
    'Separate game folders': 'Carpetas de juego separadas',
    'Only when the games are not in one collection folder (each dumped on its own). A game with a folder here is taken from it; the others from the collection folder above.':
        'Solo si los juegos no están en una única carpeta de la colección (cada uno volcado por separado). Un juego con carpeta aquí se toma de ella; los demás, de la carpeta de la colección de arriba.',
    'The folder of this game': 'La carpeta de este juego',
    'Empty: the collection folder.': 'Vacía: la carpeta de la colección.',
    'No saves yet': 'Aún no hay partidas',
    '1 save, last saved {}': '1 partida, guardada por última vez {}',
    '{} saves, last saved {}': '{} partidas, guardada por última vez {}',
    'Shader cache': 'Caché de shaders',
    'Compiled shaders are kept so the game does not stutter again. Clear the cache only when the picture is broken after an update.':
        'Los shaders compilados se guardan para que el juego no vuelva a dar tirones. Borra la caché solo si la imagen sale rota tras una actualización.',
    'Recommended to only clear if you have visual issues': 'Bórrala solo si tienes fallos visuales',
    'Shader cache cleared. The next start stutters for a while as it is rebuilt.':
        'Caché de shaders borrada. El próximo arranque tendrá tirones un rato mientras se reconstruye.',

    # ---- console ----
    'Console': 'Consola',
    'Language': 'Idioma',
    'The language the PS4 would be set to; the games use it for text and speech.':
        'El idioma que tendría la PS4; los juegos lo usan para textos y voces.',
    'Player name': 'Nombre del jugador',
    'Shown where the game uses the PSN name.': 'Se muestra donde el juego usa el nombre de PSN.',
    'Launcher language': 'Idioma del launcher',
    'The language of this window. The launcher opens again in it when you save.':
        'El idioma de esta ventana. El launcher se vuelve a abrir en ese idioma al guardar.',
    'Automatic (system language)': 'Automático (idioma del sistema)',
    'English (United States)': 'Inglés (Estados Unidos)',
    'English (United Kingdom)': 'Inglés (Reino Unido)',
    'French': 'Francés',
    'French (Canada)': 'Francés (Canadá)',
    'German': 'Alemán',
    'Italian': 'Italiano',
    'Spanish': 'Español',
    'Spanish (Latin America)': 'Español (Latinoamérica)',
    'Portuguese': 'Portugués',
    'Portuguese (Brazil)': 'Portugués (Brasil)',
    'Dutch': 'Neerlandés',
    'Russian': 'Ruso',
    'Polish': 'Polaco',
    'Turkish': 'Turco',
    'Arabic': 'Árabe',
    'Japanese': 'Japonés',
    'Korean': 'Coreano',
    'Chinese (Simplified)': 'Chino (simplificado)',
    'Chinese (Traditional)': 'Chino (tradicional)',
    'Finnish': 'Finlandés',
    'Swedish': 'Sueco',
    'Danish': 'Danés',
    'Norwegian': 'Noruego',

    # ---- display ----
    'Display': 'Pantalla',
    'How the game window is shown.': 'Cómo se muestra la ventana del juego.',
    'Fullscreen': 'Pantalla completa',
    'Presentation': 'Presentación',
    'Mailbox shows each frame as soon as it is ready without tearing; FIFO waits for the display.':
        'Mailbox muestra cada imagen en cuanto está lista, sin cortes; FIFO espera a la pantalla.',
    'Mailbox (low latency, no tearing)': 'Mailbox (baja latencia, sin cortes)',
    'FIFO (VSync)': 'FIFO (VSync)',
    'FIFO Relaxed': 'FIFO relajado',
    'Immediate (tearing)': 'Inmediato (con cortes)',
    'Frame rate': 'Fotogramas por segundo',
    'Gameplay keeps its normal speed at every setting; the cutscene videos always play at 60 FPS. Above your display\'s refresh rate use Mailbox or Immediate.':
        'El juego mantiene su velocidad normal con cualquier ajuste; los vídeos de las escenas siempre van a 60 FPS. Por encima de la frecuencia de tu pantalla usa Mailbox o Inmediato.',
    'Your display runs at {} Hz.': 'Tu pantalla funciona a {} Hz.',
    '60 FPS (as on PS4)': '60 FPS (como en PS4)',
    '60 FPS (Default)': '60 FPS (por defecto)',
    '120 FPS': '120 FPS',
    '144 FPS (experimental)': '144 FPS (experimental)',
    '165 FPS (experimental)': '165 FPS (experimental)',
    '240 FPS (experimental)': '240 FPS (experimental)',
    '30 FPS': '30 FPS',
    '40 FPS (recommended)': '40 FPS (recomendado)',
    '45 FPS': '45 FPS',
    '60 FPS': '60 FPS',
    'No limit': 'Sin límite',
    'A frame rate limit: 40 FPS paces evenly on 120 Hz screens. Also in the in-game menu.':
        'Un límite de fotogramas: 40 FPS va fluido en pantallas de 120 Hz. También en el menú del juego.',
    'Resolution': 'Resolución',
    'Window size': 'Tamaño de ventana',
    'The games render at 1920 × 1080. A larger window, or fullscreen on a 1440p or 4K screen, gets the image upscaled to fit.':
        'Los juegos se renderizan a 1920 × 1080. Una ventana más grande, o la pantalla completa en una pantalla 1440p o 4K, recibe la imagen escalada para ajustarse.',
    'Game renders at 1920 x 1080 and is upscaled at higher resolutions':
        'El juego se renderiza a 1920 × 1080 y se escala a resoluciones mayores',
    'Only for windowed mode: in fullscreen the game fills the screen. The resolution the games render at is "Render at 1280 × 720" above.':
        'Solo para el modo ventana: en pantalla completa el juego ocupa toda la pantalla. La resolución a la que se renderizan los juegos es "Renderizar a 1280 × 720", más arriba.',
    '1280 × 720': '1280 × 720',
    '1600 × 900': '1600 × 900',
    "1920 × 1080 (the games' own)": '1920 × 1080 (la de los juegos)',
    '1920 × 1080 (Default)': '1920 × 1080 (por defecto)',
    '1280 × 800 (Steam Deck)': '1280 × 800 (Steam Deck)',
    '2560 × 1440': '2560 × 1440',
    '3840 × 2160 (4K)': '3840 × 2160 (4K)',
    'Render at 1280 × 720': 'Renderizar a 1280 × 720',
    'The games render at 720p and the upscaling below scales them to the screen: much higher FPS on ARM handhelds. Also in the in-game menu (L3+R3).':
        'Los juegos se renderizan a 720p y el escalado de abajo los ajusta a la pantalla: muchos más FPS en consolas portátiles ARM. También en el menú del juego (L3+R3).',
    'Upscaling': 'Escalado',
    'FSR 1 keeps edges sharp when the image is enlarged; bilinear is softer.':
        'FSR 1 mantiene los bordes nítidos al ampliar la imagen; el bilineal es más suave.',
    'FSR 1 (AMD FidelityFX, sharp)': 'FSR 1 (AMD FidelityFX, nítido)',
    'Smooth (bilinear)': 'Suave (bilineal)',
    'Sharpening': 'Nitidez',
    'AMD RCAS after FSR 1 (only while the image is enlarged).': 'AMD RCAS después de FSR 1 (solo mientras se amplía la imagen).',
    'Off': 'Desactivado',
    'Low': 'Baja',
    'Medium': 'Media',
    'High': 'Alta',
    'Maximum': 'Máxima',
    'Anti-aliasing': 'Antialiasing',
    "Off (the game's own)": 'Desactivado (el del juego)',
    'Off (Default)': 'Desactivado (por defecto)',
    'TAA': 'TAA',
    'AMD FSR 3.1 (Native AA)': 'AMD FSR 3.1 (AA nativo)',
    'AMD FSR 4 (Native AA)': 'AMD FSR 4 (AA nativo)',
    'NVIDIA DLSS (DLAA, GeForce RTX)': 'NVIDIA DLSS (DLAA, GeForce RTX)',
    "Temporal anti-aliasing at the games' 1920 × 1080, with their own motion vectors: smoother edges and less shimmering than the games' own. DLSS needs a GeForce RTX; FSR 4 runs best on a Radeon RX 7000/9000 or a GeForce RTX 40/50.":
        'Antialiasing temporal a los 1920 × 1080 de los juegos, con sus propios vectores de movimiento: bordes más suaves y menos parpadeo que el de los juegos. DLSS necesita una GeForce RTX; FSR 4 va mejor en una Radeon RX 7000/9000 o una GeForce RTX 40/50.',
    "Frames made between two of the game's, for smoother motion: 60 FPS shows as 120. Use Mailbox or Immediate presentation and a display of at least that refresh rate. Adds a little input latency; menus and cutscene videos are not generated. Use it with the 60 or 120 FPS frame rate: above about 165 FPS it pauses.":
        'Fotogramas creados entre dos del juego, para un movimiento más fluido: 60 FPS se ven como 120. Usa la presentación Mailbox o Inmediato y una pantalla de al menos esa frecuencia. Añade algo de retraso al control; los menús y los vídeos no se generan. Úsalo con 60 o 120 FPS: por encima de unos 165 FPS se detiene.',
    'Anti-aliasing sharpness': 'Nitidez del antialiasing',
    'Frame generation': 'Generación de fotogramas',
    'Multiplier': 'Multiplicador',
    'AMD FSR 3 Frame Generation (any GPU)': 'Generación de fotogramas AMD FSR 3 (cualquier GPU)',
    'NVIDIA DLSS Frame Generation (GeForce RTX 40/50)': 'Generación de fotogramas NVIDIA DLSS (GeForce RTX 40/50)',
    '2× (one generated frame)': '2× (un fotograma generado)',
    '3× (DLSS on GeForce RTX 50)': '3× (DLSS en GeForce RTX 50)',
    '4× (DLSS on GeForce RTX 50)': '4× (DLSS en GeForce RTX 50)',
    'FSR 3 makes one frame (2×). DLSS makes up to three on a GeForce RTX 50 (4×), one on an RTX 40.':
        'FSR 3 genera un fotograma (2×). DLSS genera hasta tres en una GeForce RTX 50 (4×) y uno en una RTX 40.',
    'Show the FPS counter': 'Mostrar el contador de FPS',
    'Steam Deck mode': 'Modo Steam Deck',
    'Optimizes settings for Steam Deck. 1280x800 60FPS': 'Optimiza los ajustes para Steam Deck. 1280x800 a 60 FPS',
    'Recommended for ARM handhelds': 'Recomendado para portátiles ARM',
    'Puts every setting on this page back to what runs best on an ARM handheld: 720p with FSR 1, a 40 FPS limit, fullscreen and all the compatibility fixes on.':
        'Vuelve a poner todos los ajustes de esta página como mejor funcionan en una portátil ARM: 720p con FSR 1, límite de 40 FPS, pantalla completa y todos los arreglos de compatibilidad activados.',
    'Recommended settings applied and saved.': 'Ajustes recomendados aplicados y guardados.',

    # ---- controls ----
    'Mouse': 'Ratón',
    'Look around with the mouse': 'Mirar alrededor con el ratón',
    'The cursor stays in the game window; hold Left Alt (or switch windows) to free it. Insert opens the in-game settings menu.':
        'El cursor se queda en la ventana del juego; mantén Alt izquierdo (o cambia de ventana) para liberarlo. Insert abre el menú de ajustes del juego.',
    'Mouse sensitivity': 'Sensibilidad del ratón',
    'How far the camera turns for a mouse movement; 1.00 is the default. Drag the slider or type a number (0.1 to 10) for a faster turn.':
        'Cuánto gira la cámara por cada movimiento del ratón; 1.00 es el valor por defecto. Arrastra el control o escribe un número (de 0.1 a 10) para un giro más rápido.',
    'Invert the mouse vertically': 'Invertir el ratón en vertical',
    'Keyboard and mouse buttons': 'Teclado y botones del ratón',
    'Click a box, then press a key or a mouse button (the wheel counts too). ✕ removes a binding. Each action can have two.':
        'Haz clic en una casilla y pulsa una tecla o un botón del ratón (la rueda también cuenta). ✕ quita la asignación. Cada acción puede tener dos.',
    'Controller': 'Mando',
    'Which controller button gives each PS4 button (Xbox / PlayStation names).':
        'Qué botón del mando hace de cada botón de PS4 (nombres de Xbox / PlayStation).',
    'Invert the right stick vertically': 'Invertir el stick derecho en vertical',
    'A / Cross': 'A / Cruz',
    'B / Circle': 'B / Círculo',
    'X / Square': 'X / Cuadrado',
    'Y / Triangle': 'Y / Triángulo',
    'LB / L1': 'LB / L1',
    'RB / R1': 'RB / R1',
    'LT / L2': 'LT / L2',
    'RT / R2': 'RT / R2',
    'Left stick click / L3': 'Pulsar stick izquierdo / L3',
    'Right stick click / R3': 'Pulsar stick derecho / R3',
    'Start / Options': 'Start / Options',
    'Back / Share': 'Back / Share',
    'Touchpad click': 'Pulsar el panel táctil',
    'D-pad up': 'Cruceta arriba',
    'D-pad down': 'Cruceta abajo',
    'D-pad left': 'Cruceta izquierda',
    'D-pad right': 'Cruceta derecha',
    'Share / Mic (misc)': 'Share / Micro (otros)',
    'Paddle 1': 'Pala 1',
    'Paddle 2': 'Pala 2',
    'Paddle 3': 'Pala 3',
    'Paddle 4': 'Pala 4',
    'Not assigned': 'Sin asignar',
    '1.0 (default)': '1.0 (por defecto)',
    'Controller in the launcher: D-pad or left stick to move, A to choose, left/right to change a value, LB/RB to change page, B for the Library.':
        'Mando en el launcher: cruceta o stick izquierdo para moverte, A para elegir, izquierda/derecha para cambiar un valor, LB/RB para cambiar de página y B para volver a la Biblioteca.',

    # ---- advanced ----
    'Close the launcher when the game starts': 'Cerrar el launcher cuando empiece el juego',
    'Environment': 'Variables de entorno',
    'Extra variables for the game, NAME=value separated by spaces.':
        'Variables extra para el juego, NOMBRE=valor separadas por espacios.',
    'Updates': 'Actualizaciones',
    'Automatic updates': 'Actualizaciones automáticas',
    'Look for a new version when the launcher starts': 'Buscar una versión nueva al abrir el launcher',
    'Look for a new version when the launcher or a game starts, and offer to install it.':
        'Buscar una versión nueva al abrir el launcher o un juego, y ofrecer instalarla.',
    'Logs and bug reports': 'Registros e informes de errores',
    'Logs of the last 5 launcher and game runs. Create bug report saves them with your system details and settings as one zip file in Downloads, without your home folder or user name; nothing is uploaded. If the launcher window does not work, right-click Uncharted in the application menu and choose Create bug report.':
        'Registros de las últimas 5 ejecuciones del launcher y del juego. Crear informe de error los guarda con los datos de tu sistema y tus ajustes en un único zip en Descargas, sin tu carpeta personal ni tu nombre de usuario; no se sube nada. Si la ventana del launcher no funciona, haz clic derecho en Uncharted en el menú de aplicaciones y elige Crear informe de error.',
    'No problems found in the recent logs.': 'No se han encontrado problemas en los registros recientes.',
    'Found in the recent logs:': 'Encontrado en los registros recientes:',
    'Compatibility (ARM)': 'Compatibilidad (ARM)',
    "The ARM port's performance fixes. All on is the fastest; if a scene misbehaves, turn the one that fits off and tell us where it happened.":
        'Los arreglos de rendimiento del port a ARM. Con todos activados va más rápido; si una escena falla, desactiva el que corresponda y cuéntanos dónde pasó.',
    'Fast GPU memory reads (Uncharted 2 and 3)': 'Lecturas rápidas de la memoria de la GPU (Uncharted 2 y 3)',
    'Send GPU work in small batches (Uncharted 2 and 3)': 'Enviar el trabajo a la GPU en tandas pequeñas (Uncharted 2 y 3)',
    'Let idle game threads sleep': 'Dejar dormir a los hilos del juego sin trabajo',
    'Turn off if, in Uncharted 2 or 3, characters float or slide over the ground, explosions or vehicle damage do not behave as they should, or surfaces flash in flat colours (cyan, white). With it off the game waits for the GPU at those reads: slower, as in the original port.':
        'Desactívalo si, en Uncharted 2 o 3, los personajes flotan o se deslizan por el suelo, las explosiones o los daños de los vehículos no se comportan como deberían, o algunas superficies parpadean con colores planos (cian, blanco). Desactivado, el juego espera a la GPU en esas lecturas: más lento, como en el port original.',
    'Turn off if a scene freezes (the picture stops while the sound goes on) or the game hangs at the same spot every time. With it off Uncharted 2 and 3 run at a somewhat lower frame rate.':
        'Desactívalo si una escena se congela (la imagen se para mientras el sonido sigue) o el juego se cuelga siempre en el mismo sitio. Desactivado, Uncharted 2 y 3 van con algo menos de FPS.',
    'Turn off if the game stutters or pauses for a moment again and again in one place, or loading screens take much longer than usual. With it off the game uses about twice the CPU: more battery and heat.':
        'Desactívalo si el juego da tirones o se detiene un momento una y otra vez en un mismo sitio, o si las pantallas de carga tardan mucho más de lo normal. Desactivado, el juego usa más o menos el doble de CPU: más batería y más calor.',

    # ---- messages ----
    "Uncharted stopped with:\n\n{}\n\nOpen a crash report on the Uncharted port's GitHub? Your system details and the end of the log are filled in; nothing is sent until you submit it. Please check its open issues for the same crash first.":
        'Uncharted se detuvo con:\n\n{}\n\n¿Abrir un informe del fallo en el GitHub del port de Uncharted? Los datos de tu sistema y el final del registro se rellenan solos; no se envía nada hasta que lo publiques. Antes, mira si ya hay una incidencia abierta con el mismo fallo.',
    'Preparing the game; it opens in its own window…': 'Preparando el juego; se abrirá en su propia ventana…',
    'The game is running.': 'El juego está en marcha.',
    'Starting the next game of the collection…': 'Iniciando el siguiente juego de la colección…',
    'Could not start: {}': 'No se pudo iniciar: {}',
    '\n— the game exited (code {}) —\n': '\n— el juego se cerró (código {}) —\n',
    'The game stopped: {}': 'El juego se detuvo: {}',
    'see the Log page': 'mira la página Registro',
    'Close the game before restarting.': 'Cierra el juego antes de reiniciar.',
    'Close the game before updating.': 'Cierra el juego antes de actualizar.',
    'Version {} is available.': 'La versión {} está disponible.',
    '{} available': '{} disponible',
    'You have the latest version ({}).': 'Tienes la última versión ({}).',
    'Could not check for updates: {}': 'No se pudieron buscar actualizaciones: {}',
    'Install version {} now? The launcher closes, installs it and opens again. Saves and settings are kept.':
        '¿Instalar ahora la versión {}? El launcher se cierra, la instala y se vuelve a abrir. Se conservan las partidas y los ajustes.',
    'Downloading version {}… {}%': 'Descargando la versión {}… {}%',
    'Downloading {}%': 'Descargando {}%',
    'Update failed: {}': 'La actualización falló: {}',
    'Could not install the update. Download it from the releases page.':
        'No se pudo instalar la actualización. Descárgala desde la página de versiones.',
    'Shortcut created on the desktop.': 'Acceso directo creado en el escritorio.',
    '{} is in the application menu and on the desktop.': '{} está en el menú de aplicaciones y en el escritorio.',
    'Steam was not found on this PC (or nobody has signed in to it yet).':
        'No se ha encontrado Steam en este equipo (o nadie ha iniciado sesión todavía).',
    'Steam has to close while the game is added; it starts again afterwards. Close Steam now?':
        'Steam tiene que cerrarse mientras se añade el juego; después se vuelve a abrir. ¿Cerrar Steam ahora?',
    'Steam did not close. Close it yourself and try again.': 'Steam no se ha cerrado. Ciérralo tú y vuelve a intentarlo.',
    'Could not add the game to Steam: {}': 'No se pudo añadir el juego a Steam: {}',
    'The collection is in your Steam library. It starts with the settings saved in this launcher.':
        'La colección está en tu biblioteca de Steam. Se inicia con los ajustes guardados en este launcher.',
    '{} is in your Steam library. It starts with the settings saved in this launcher.':
        '{} está en tu biblioteca de Steam. Se inicia con los ajustes guardados en este launcher.',
    'Could not create the bug report: {}': 'No se pudo crear el informe de error: {}',
    'Bug report saved as {} in {}.\n\nSend this file with your bug report (Discord or a GitHub issue).':
        'Informe de error guardado como {} en {}.\n\nEnvía este archivo con tu informe (Discord o una incidencia en GitHub).',
}
