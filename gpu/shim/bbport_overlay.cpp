// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <SDL3/SDL.h>
#include "bbport_settings.h"

extern "C" void runtime_restart(void); // bb-probe (probe.c): restarts through BB_RESTART_COMMAND
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

// DejaVu Sans (Cyrillic), embedded (third_party/fonts, Bitstream Vera license).
#ifdef _WIN32
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".text\n");
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_ttf\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".hidden bb_font_ttf_end\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".previous\n");
#endif
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];

namespace BbOverlay {

namespace {

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<bool> menu_open{false};
bool l3_down = false, r3_down = false;
bool dirty = false; // settings changed while open: saved on close
// The game's text dialog (SetTextEntry), guarded by imgui_mutex.
bool text_entry_active = false;
std::string text_entry_prompt, text_entry_text;
float base_scale = 1.0f;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f, last_present_ms = 0.0f;
// The game's own frames alone (frame generation shows more than the game makes).
std::chrono::steady_clock::time_point last_original{};
float game_ms_avg = 0.0f;

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) {
        return;
    }
    ImGui::GetIO().MouseDrawCursor = value;
    if (!value && dirty) {
        dirty = false;
        BbSettings::Save();
    }
}

ImGuiKey KeyFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

// Marks the settings dirty when a widget changed them.
template <typename T>
void Store(std::atomic<T>& target, T value, bool changed) {
    if (changed) {
        target = value;
        dirty = true;
    }
}

void Checkbox(const char* label, std::atomic<bool>& value) {
    // The widget first, then the copy of v: the order of function arguments is unspecified, and
    // on aarch64 v was copied before ImGui changed it (checkboxes and sliders never changed).
    bool v = value;
    const bool changed = ImGui::Checkbox(label, &v);
    Store(value, v, changed);
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi) {
    float v = value;
    const bool changed = ImGui::SliderFloat(label, &v, lo, hi, "%.2f");
    Store(value, v, changed);
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Linux fork: the menu follows the game language picked in the launcher (BB_LANGUAGE, the PS4
// numbering): Russian for 8, English for every other language.
const char* T(const char* en, const char* ru) {
    static const bool russian = [] {
        const char* language = std::getenv("BB_LANGUAGE");
        return language && std::strcmp(language, "8") == 0;
    }();
    return russian ? ru : en;
}

// Linux fork: only the settings that act in Uncharted. Its velocity mode runs anti-aliasing at
// the game's own 1920x1080 (no presets, no output size), and the Bloodborne patch effects
// (patches/Bloodborne.xml) are never applied by run.py.
void Menu() {
    auto& s = BbSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40.0f * base_scale,
                                   viewport->WorkPos.y + 40.0f * base_scale),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(620.0f * base_scale, 0.0f), ImGuiCond_Appearing);
    bool keep_open = true;
    if (!ImGui::Begin(T("Uncharted — settings  (Insert / L3+R3)###menu",
                        "Uncharted — настройки  (Insert / L3+R3)###menu"),
                      &keep_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ImGui::Text(T("%.0f FPS  (%.1f ms)", "%.0f FPS  (%.1f мс)"),
                frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f, frame_ms_avg);

    ImGui::SeparatorText(T("Frame rate", "Частота кадров"));
    {
        int limit_index = 0;
        for (int i = 0; i < BbSettings::FpsLimitCount; ++i) {
            if (BbSettings::FpsLimits[i] == s.fps_limit) limit_index = i;
        }
        static const char* limit_names[] = {"Off", "30 FPS", "40 FPS (120 Hz screens)", "45 FPS",
                                            "60 FPS"};
        static_assert(sizeof(limit_names) / sizeof(*limit_names) == BbSettings::FpsLimitCount);
        if (ImGui::BeginCombo(T("Frame rate limit", "Ограничение FPS"), limit_names[limit_index])) {
            for (int i = 0; i < BbSettings::FpsLimitCount; ++i) {
                if (ImGui::Selectable(limit_names[i], i == limit_index)) {
                    Store(s.fps_limit, BbSettings::FpsLimits[i], true);
                }
            }
            ImGui::EndCombo();
        }
        Hint(T("A steady rate the GPU can always reach feels smoother than a varying higher one, "
               "and saves battery and heat. 40 FPS shows every frame for exactly 3 refreshes of a "
               "120 Hz screen. Applies immediately.",
               "Ровная частота, которую GPU всегда держит, плавнее скачущей более высокой и "
               "экономит батарею. 40 FPS — ровно 3 обновления экрана 120 Гц. Сразу."));
    }

    // Uncharted ARM port: one list for the scaling (FSR 1 or bilinear to the screen) and the
    // temporal upscalers (FSR 3.1, GSR 2, ...), with their quality presets. A preset below Native
    // makes the game draw at 1920x1080 / scale (run.py, run-uncharted.sh: the resolution patch on
    // restart) and the upscaler bring each frame to 1920x1080; the HUD stays at 1920x1080.
    ImGui::SeparatorText(T("Upscaling", "Масштабирование"));
    struct Method {
        const char* name;
        int upscaler;
        bool fsr1;
    };
    const Method methods[] = {
        {T("Off (bilinear scaling)", "Выкл (билинейное)"), BbSettings::UpscalerOff, false},
        {T("AMD FSR 1 (spatial)", "AMD FSR 1 (пространственное)"), BbSettings::UpscalerOff, true},
        {"AMD FSR 3.1", BbSettings::UpscalerFsr3, true},
        {"Snapdragon GSR 2", BbSettings::UpscalerGsr2, true},
        {T("TAA (anti-aliasing only)", "TAA (только сглаживание)"), BbSettings::UpscalerTaa, true},
        {"AMD FSR 4 (INT8)", BbSettings::UpscalerFsr4, true},
        {"AMD FSR 4.1.1 (INT8)", BbSettings::UpscalerFsr411, true},
        {"DLSS (NVIDIA RTX)", BbSettings::UpscalerDlss, true},
    };
    const char* unsupported = T("— not supported by this GPU", "— не поддерживается видеокартой");
    const int upscaler = s.upscaler;
    int method = 0;
    for (int i = 0; i < int(sizeof(methods) / sizeof(*methods)); ++i) {
        if (methods[i].upscaler == upscaler &&
            (upscaler != BbSettings::UpscalerOff || methods[i].fsr1 == s.fsr1.load())) {
            method = i;
            break;
        }
    }
    if (ImGui::BeginCombo(T("Method", "Метод"), methods[method].name)) {
        for (int i = 0; i < int(sizeof(methods) / sizeof(*methods)); ++i) {
            const int u = methods[i].upscaler;
            const bool supported = u == BbSettings::UpscalerFsr4 ? s.fsr4_supported.load()
                : u == BbSettings::UpscalerFsr411 ? s.fsr411_supported.load()
                : u == BbSettings::UpscalerDlss   ? s.dlss_supported.load() : true;
            if (!supported) {
                continue; // FSR 4 and DLSS: only on the GPUs that run them
            }
            if (ImGui::Selectable(methods[i].name, i == method)) {
                Store(s.upscaler, u, true);
                Store(s.fsr1, methods[i].fsr1, true);
            }
        }
        ImGui::EndCombo();
    }
    const bool temporal_scaler = upscaler != BbSettings::UpscalerOff && upscaler != BbSettings::UpscalerTaa;
    Hint(T("FSR 1 enlarges the frame alone: sharp, no anti-aliasing, almost free. FSR 3.1 and GSR 2 "
           "(Snapdragon Game Super Resolution, made for Adreno GPUs) rebuild each frame with the "
           "game's depth and motion: they smooth edges and, below Native, draw fewer pixels and "
           "rebuild 1920x1080. GSR 2 costs less GPU time than FSR 3.1. TAA only smooths edges. "
           "The method applies on the next frame.",
           "FSR 1 только растягивает кадр: чётко, без сглаживания. FSR 3.1 и GSR 2 восстанавливают "
           "кадр по глубине и движению: сглаживают и ниже Native рисуют меньше пикселей. TAA только "
           "сглаживает. Метод применяется со следующего кадра."));
    {
        static const char* presets[] = {
            T("Native (anti-aliasing at the render resolution)", "Native (сглаживание)"),
            T("Quality (1280 x 720 to 1080p)", "Качество (1280 x 720)"),
            T("Balanced (1136 x 636 to 1080p)", "Баланс (1136 x 636)"),
            T("Performance (960 x 540 to 1080p)", "Производительность (960 x 540)"),
            T("Ultra Performance (640 x 360 to 1080p)", "Ультра (640 x 360)")};
        static_assert(sizeof(presets) / sizeof(*presets) == BbSettings::PresetCount);
        const int preset = std::clamp(s.preset.load(), 0, BbSettings::PresetCount - 1);
        ImGui::BeginDisabled(!temporal_scaler);
        if (ImGui::BeginCombo(T("Quality", "Качество"), presets[preset])) {
            for (int i = 0; i < BbSettings::PresetCount; ++i) {
                if (ImGui::Selectable(presets[i], i == preset)) {
                    Store(s.preset, i, true);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        Hint(T("For FSR 3.1 and GSR 2. Native: they only smooth edges at the render resolution below. "
               "The others choose the render size themselves (1920x1080 divided by 1.5, 1.7, 2 or 3) "
               "and upscale to 1920x1080, with the HUD and menus at 1920x1080. Applies when the game "
               "restarts.",
               "Для FSR 3.1 и GSR 2. Native: только сглаживание. Остальные сами выбирают размер "
               "рендера и масштабируют до 1920x1080. После перезапуска."));
    }
    const bool preset_scaling = temporal_scaler && s.preset != BbSettings::NativeAA;
    // The size the whole game renders at, on restart: its own 1080p, its own PS4 720p mode
    // (render_720p, scripts/render_720p.py) or 960x540 (render_540p, scripts/render_res.py).
    {
        static const char* sizes[] = {
            T("1920 x 1080 (the game's own)", "1920 x 1080 (родное)"),
            T("1280 x 720 (the game's PS4 720p mode)", "1280 x 720 (режим 720p PS4)"),
            T("960 x 540 (slower GPUs)", "960 x 540 (слабые GPU)")};
        const int current = s.render_720p ? 1 : s.render_540p ? 2 : 0;
        ImGui::BeginDisabled(preset_scaling);
        if (ImGui::BeginCombo(T("Render resolution", "Разрешение рендера"), sizes[current])) {
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(sizes[i], i == current)) {
                    Store(s.render_720p, i == 1, true);
                    Store(s.render_540p, i == 2, true);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    }
    Hint(preset_scaling
             ? T("Set by the quality above while FSR 3.1 or GSR 2 upscale.",
                 "Задаётся качеством выше, пока работает FSR 3.1 или GSR 2.")
             : T("The size the whole game draws at (scene, effects, UI and movies); the frame is then "
                 "enlarged to the screen with the method above. 1280x720 is the game's own PS4 720p "
                 "mode: much faster on handhelds, a little softer. 960x540 is for GPUs that 720p is "
                 "still too much for. Applies when the game restarts.",
                 "Размер, в котором рисуется вся игра; кадр растягивается на экран. 1280x720 — режим "
                 "720p PS4. 960x540 — для GPU, которым мало 720p. После перезапуска."));
    // The game's render size follows a preset only from the next start: switching between an
    // upscaler with a preset and anything else (FSR 1, TAA, Native) needs a restart too.
    const auto scaling = [](int u, int p) {
        return u != BbSettings::UpscalerOff && u != BbSettings::UpscalerTaa && p != BbSettings::NativeAA;
    };
    const bool scaling_changed = scaling(s.upscaler, s.preset) != scaling(s.startup_upscaler, s.startup_preset) ||
                                 (scaling(s.upscaler, s.preset) && s.preset != s.startup_preset);
    if (s.render_720p != s.startup_render_720p || s.render_540p != s.startup_render_540p ||
        scaling_changed) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s",
                           T("Applies when the game restarts (Save and restart, below)",
                             "Применится после перезапуска (кнопка ниже)"));
    }
    if (const char* problem = s.dlss_problem.load(); problem && upscaler == BbSettings::UpscalerDlss) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "DLSS: %s", problem);
    }
    if (const char* problem = s.fsr4_problem.load()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                           T("FSR 4 unavailable: %s", "FSR 4 недоступен: %s"), problem);
        if (!BbSettings::IsFsr4(s.upscaler))
            ImGui::TextUnformatted(T("The method chosen above is active. FSR 4 can be picked again.",
                                     "Активен метод, выбранный выше. FSR 4 можно выбрать снова."));
        ImGui::PopTextWrapPos();
    }
    if (BbSettings::IsFsr4(s.upscaler)) {
        Checkbox(T("FSR 4: auto exposure", "FSR 4: авто-экспозиция"), s.fsr4_auto_exposure);
        Checkbox(T("FSR 4: invert jitter sign", "FSR 4: обратный знак jitter"), s.fsr4_invert_jitter);
        Hint(T("For checking ghosting: the FSR 4 network normalises colour by exposure and uses it to "
               "decide when to drop past frames. Both apply immediately.",
               "Проверка при гостинге: сеть FSR 4 нормирует цвет по экспозиции и по ней решает, "
               "когда отбросить прошлые кадры. Меняются сразу, без перезапуска."));
    }
    const bool upscaler_on = s.upscaler != BbSettings::UpscalerOff;
    const bool taa = s.upscaler == BbSettings::UpscalerTaa;
    ImGui::BeginDisabled(!upscaler_on);
    Checkbox(T("Sharpening (RCAS)", "Резкость (RCAS)"), s.sharpen);
    ImGui::BeginDisabled(!s.sharpen);
    Slider(T("Strength", "Сила резкости"), s.sharpness, 0.0f, 2.0f);
    Hint(T("Up to 1: the anti-aliasing's own sharpening (RCAS). Above 1 adds another RCAS pass. "
           "Ctrl+click the slider to type an exact value.",
           "До 1 — резкость самого сглаживания (RCAS). Выше 1 добавляется ещё один проход RCAS. "
           "Ctrl+клик по ползунку — ввести точное значение."));
    ImGui::EndDisabled();
    Checkbox(T("Sub-pixel jitter", "Субпиксельный сдвиг (jitter)"), s.jitter);
    Hint(T("Shifts the scene by a fraction of a pixel each frame so detail builds up over several "
           "frames. Without it, only history smoothing remains.",
           "Каждый кадр сцена сдвигается на долю пикселя, и из нескольких кадров собирается "
           "больше деталей. Без него остаётся только сглаживание по истории."));

    ImGui::SeparatorText(T("Reactive mask", "Маска реактивности"));
    ImGui::BeginDisabled(taa);
    Checkbox(T("Enable the mask", "Включить маску"), s.reactive);
    Hint(T("Marks transparent effects (particles, haze) so less of their past frames is kept: "
           "fewer trails behind effects, but more shimmer under them.",
           "Помечает прозрачные эффекты (частицы, дымку), чтобы меньше опираться на прошлые "
           "кадры. Меньше шлейфов за эффектами, но под ними возвращается дрожание."));
    ImGui::BeginDisabled(!s.reactive);
    Slider(T("Scale", "Масштаб"), s.reactive_scale, 0.0f, 4.0f);
    Slider(T("Threshold", "Порог"), s.reactive_threshold, 0.0f, 1.0f);
    Slider(T("Maximum", "Максимум"), s.reactive_max, 0.0f, 1.0f);
    bool show_mask = s.debug_view == BbSettings::DebugReactive;
    if (ImGui::Checkbox(T("Show the mask (debug)", "Показать маску (отладка)"), &show_mask)) {
        s.debug_view = show_mask ? BbSettings::DebugReactive : BbSettings::DebugNone;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    Checkbox(T("Character motion vectors", "Векторы движения персонажей"), s.object_motion);
    Hint(T("Exact motion for animated objects, so clothing and weapons break up less in motion. "
           "Applies the next time the game starts.",
           "Точные векторы для анимированных объектов: одежда и оружие меньше рассыпаются "
           "при движении. Применяется при следующем запуске игры."));
    bool show_motion = s.debug_view == BbSettings::DebugMotion;
    if (ImGui::Checkbox(T("Show motion vectors (debug)", "Показать векторы движения (отладка)"),
                        &show_motion)) {
        s.debug_view = show_motion ? BbSettings::DebugMotion : BbSettings::DebugNone;
    }
    Hint(T("Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
           "has an exact object vector, not only camera motion.",
           "Красный/зелёный: движение по горизонтали/вертикали (8 пикселей = полная яркость). "
           "Синий: пиксель получил точный вектор объекта, а не только движение камеры."));
    ImGui::EndDisabled(); // anti-aliasing off
    if (s.object_motion != s.startup_object_motion) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "%s", T("Applies the next time the game starts",
                                   "Применится при следующем запуске игры"));
    }

    ImGui::SeparatorText(T("Frame generation", "Генерация кадров"));
    const char* frame_gens[] = {T("Off", "Выкл"), T("FSR 3 (any GPU)", "FSR 3 (любая видеокарта)"),
                                "DLSS (NVIDIA RTX 40/50)",
                                T("LSFG (your Lossless.dll)", "LSFG (ваш Lossless.dll)")};
    const int frame_gen = s.frame_gen;
    if (ImGui::BeginCombo(T("Generation", "Генерация"), frame_gens[frame_gen])) {
        for (int i = 0; i < BbSettings::FrameGenCount; ++i) {
#ifdef __ANDROID__
            // Android: FSR 3 frame generation shows no effect there yet; not offered (kept for
            // a frame_gen=fsr3 already chosen).
            if (i == BbSettings::FrameGenFsr3 && frame_gen != i) {
                continue;
            }
#endif
            const bool supported = (i != BbSettings::FrameGenDlss || s.dlssg_max_multiplier > 1) &&
                                   (i != BbSettings::FrameGenLsfg || !s.lsfg_dll.empty());
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(frame_gens[i], i == frame_gen)) {
                Store(s.frame_gen, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", i == BbSettings::FrameGenLsfg
                                              ? T("— no Lossless.dll chosen (lsfg_dll)",
                                                  "— не выбран Lossless.dll (lsfg_dll)")
                                              : unsupported);
            }
        }
        ImGui::EndCombo();
    }
    Hint(T("Frames between two of the game's: 60 FPS on screen becomes 120. Needs the Mailbox or "
           "Immediate present mode and a display that fast. Adds a little input latency. Menus and "
           "cutscene videos are not generated.",
           "Кадры между двумя кадрами игры: 60 FPS на экране становятся 120. Нужны представление "
           "Mailbox или Immediate и монитор с такой частотой. Немного добавляет задержку ввода. "
           "Меню и видеоролики не генерируются."));
    // LSFG: up to three generated frames, like DLSS on an RTX 50.
    const int max_multiplier = s.frame_gen == BbSettings::FrameGenDlss ? s.dlssg_max_multiplier.load()
                               : s.frame_gen == BbSettings::FrameGenLsfg ? 4
                                                                         : 2;
    ImGui::BeginDisabled(s.frame_gen == BbSettings::FrameGenOff || max_multiplier <= 2);
    int multiplier = std::min(s.frame_gen_multiplier.load(), max_multiplier);
    if (ImGui::SliderInt(T("Multiplier", "Множитель"), &multiplier, 2, std::max(max_multiplier, 2), "%dx")) {
        Store(s.frame_gen_multiplier, multiplier, true);
    }
    ImGui::EndDisabled();
    if (s.frame_gen == BbSettings::FrameGenLsfg) {
        // How finely LSFG measures motion: 100 is automatic (432 lines, about 40% of a 1080p
        // screen); higher values cost more GPU and ghost less around moving characters.
        // Applied when the slider is let go: each value rebuilds the passes (a GPU idle wait).
        static int flow_edit = -1;
        int flow = flow_edit >= 0 ? flow_edit : s.lsfg_flow_scale.load();
        ImGui::SliderInt(T("LSFG motion resolution", "Разрешение движения LSFG"), &flow, 25, 100,
                         flow >= 100 ? T("auto", "авто") : "%d%%");
        flow_edit = ImGui::IsItemActive() ? flow : -1;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            Store(s.lsfg_flow_scale, flow, true);
        }
    }
    if (const char* problem = s.frame_gen_problem.load()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", problem);
    }

    ImGui::SeparatorText(T("Other", "Прочее"));
    Checkbox(T("FPS counter in the corner", "Счётчик FPS в углу"), s.show_fps);

    ImGui::Spacing();
    if (ImGui::Button(T("Close", "Закрыть"))) {
        keep_open = false;
    }
    ImGui::SameLine();
    // Changes that apply at the next start (resolution, effects, object motion): saved, and the
    // game starts again at the same executable (BB_RESTART_COMMAND).
    if (ImGui::Button(T("Save and restart the game", "Сохранить и перезапустить"))) {
        BbSettings::Save();
        dirty = false;
        runtime_restart();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("Anti-aliasing, sharpness and frame generation reset to the "
                                "launcher's choice at each start",
                                "Сглаживание, резкость и генерация кадров при каждом запуске "
                                "берутся из лаунчера"));
    ImGui::End();
    if (!keep_open) {
        SetOpen(false);
    }
}

// The game's text dialog: what is typed, and how to finish (keyboard or controller).
void TextEntryBox() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.42f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(viewport->WorkSize.x * 0.32f, 0.0f),
                                        ImVec2(viewport->WorkSize.x * 0.8f, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##text_entry", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.45f, 1.0f), "%s", text_entry_prompt.c_str());
    ImGui::Separator();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("%s_", text_entry_text.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::TextDisabled("Keyboard: type, Backspace to erase, Enter = OK, Esc = cancel");
    ImGui::TextDisabled("Controller: Cross (A) = OK, Circle (B) = cancel");
    ImGui::End();
}

void FpsCounter() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                                   viewport->WorkPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    const auto& s = BbSettings::Get();
    // Frame generation: the game's rate and what reaches the screen, "30→60 FPS LSFG 2x" (after
    // erickmendes22's uncharted-android b2c25e9); only while it does show more than the game makes.
    const float output_fps = frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f;
    const float game_fps = game_ms_avg > 0.0f ? 1000.0f / game_ms_avg : 0.0f;
    const int frame_gen = s.frame_gen.load();
    if (frame_gen != BbSettings::FrameGenOff && game_fps > 0.0f && output_fps > game_fps * 1.2f) {
        char generated[48];
        if (frame_gen == BbSettings::FrameGenLsfg) {
            std::snprintf(generated, sizeof(generated), "LSFG %dx", s.frame_gen_multiplier.load());
        } else {
            std::snprintf(generated, sizeof(generated), "%s", BbSettings::FrameGenName(frame_gen));
        }
        ImGui::Text(T("%.0f\u2192%.0f FPS  %s", "%.0f\u2192%.0f FPS  %s"), game_fps, output_fps,
                    generated);
    }
    ImGui::Text(T("%.0f FPS  %.1f ms  %s", "%.0f FPS  %.1f мс  %s"), frame_gen != BbSettings::FrameGenOff && game_fps > 0.0f ? game_fps : output_fps,
                frame_gen != BbSettings::FrameGenOff && game_ms_avg > 0.0f ? game_ms_avg : frame_ms_avg,
                s.upscaler == BbSettings::UpscalerFsr3   ? "FSR 3.1"
                : s.upscaler == BbSettings::UpscalerGsr2 ? "GSR 2"
                : s.upscaler == BbSettings::UpscalerFsr4 ? "FSR 4"
                : s.upscaler == BbSettings::UpscalerFsr411 ? "FSR 4.1.1"
                : s.upscaler == BbSettings::UpscalerTaa ? "TAA"
                : s.upscaler == BbSettings::UpscalerDlss ? "DLSS"
                                                         : "");
    ImGui::End();
}

} // namespace

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "bbport";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), 18.0f, &font_config);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (Insert or L3+R3)\n");
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const bool is_open = menu_open;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.key.repeat &&
            (event.key.key == SDLK_INSERT || (is_open && event.key.key == SDLK_ESCAPE))) {
            SetOpen(event.key.key == SDLK_INSERT ? !is_open : false);
            return true;
        }
        if (!is_open) {
            return false;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            l3_down = down;
        } else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            r3_down = down;
        }
        if (down && l3_down && r3_down) {
            SetOpen(!is_open);
            return true;
        }
        if (!is_open) {
            return false;
        }
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        // Typed characters (Ctrl+click on a slider, a text field): key events alone erase but
        // do not type. SDL sends them while text input is on (UpdateTextInput).
        if (!is_open) {
            return false;
        }
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) {
            return false;
        }
        const float density = PixelDensity(event.motion.windowID);
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

bool Visible() {
    return initialized && (menu_open || text_entry_active || BbSettings::Get().show_fps);
}

bool CapturesInput() {
    return menu_open || text_entry_active;
}

void SetTextEntry(bool active, const std::string& prompt, const std::string& text) {
    std::scoped_lock lock{imgui_mutex};
    text_entry_active = active;
    text_entry_prompt = prompt;
    text_entry_text = text;
}

// Called for every presented frame: the interval for the FPS readout and BB_FPS_LOG.
void NotePresent(bool original) {
    const auto now = std::chrono::steady_clock::now();
    if (original) {
        const float game_ms = std::chrono::duration<float, std::milli>(now - last_original).count();
        last_original = now;
        if (game_ms > 0.0f && game_ms < 1000.0f) {
            game_ms_avg = game_ms_avg == 0.0f ? game_ms : game_ms_avg * 0.95f + game_ms * 0.05f;
        }
    }
    const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
    last_present = now;
    last_present_ms = ms;
    if (ms > 0.0f && ms < 1000.0f) {
        frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
    }
    // BB_FPS_LOG=1: presents per second and the slowest frame, every 5 seconds (test runs).
    static const bool fps_log = [] {
        const char* env = std::getenv("BB_FPS_LOG");
        return env && env[0] == '1';
    }();
    if (fps_log) {
        static auto window_start = now;
        static int frames = 0;
        static float worst = 0.0f;
        ++frames;
        worst = std::max(worst, ms < 1000.0f ? ms : 0.0f);
        const float elapsed = std::chrono::duration<float>(now - window_start).count();
        if (elapsed >= 5.0f) {
            std::printf("Frame rate: %.1f FPS, slowest frame %.1f ms\n", frames / elapsed, worst);
            window_start = now;
            frames = 0;
            worst = 0.0f;
        }
    }
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent) {
    const float ms = last_present_ms;
    if (!Visible()) {
        return;
    }
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // UI scale follows the display height (1080p = 1).
    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (menu_open) {
        Menu();
    }
    if (BbSettings::Get().show_fps && !menu_open) {
        FpsCounter();
    }
    if (text_entry_active) {
        TextEntryBox();
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay
