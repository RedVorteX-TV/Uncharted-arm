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

#ifndef __ANDROID__ // Android: no SDL; the pad comes through HandlePad (runtime_pad.c)
#include <SDL3/SDL.h>
#endif
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

#ifndef __ANDROID__
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

#endif // !__ANDROID__

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

    ImGui::SeparatorText(T("Resolution", "Разрешение"));
    Checkbox(T("Render the whole game at 1280x720 (restart)", "Вся игра в 1280x720 (перезапуск)"),
             s.render_720p);
    Hint(T("The game switches to its own PS4 720p mode: scene, effects, UI and movies draw at "
           "1280x720 and the frame is enlarged to the screen. Much faster on handhelds, a little "
           "softer. From spacepepperoni's Linux fork (its Steam Deck mode).",
           "Игра переходит в свой режим 720p PS4: всё рисуется в 1280x720 и растягивается на "
           "экран. Намного быстрее на портативных устройствах, чуть мягче."));
    if (s.render_720p != s.startup_render_720p) {
        if (ImGui::Button(T("Apply and restart the game", "Применить и перезапустить игру"))) {
            BbSettings::Save();
            runtime_restart();
        }
    }
    Checkbox(T("FSR 1 (sharp scaling to the screen)", "FSR 1 (чёткое масштабирование)"), s.fsr1);
    Hint(T("How a frame smaller than the screen (the 1280x720 mode on a 1080p screen) is enlarged: "
           "AMD FSR 1 keeps edges sharp, off uses a softer bilinear scaling. Costs a fraction of a "
           "millisecond. Applies immediately.",
           "Как кадр меньше экрана растягивается: AMD FSR 1 сохраняет чёткость, без него — "
           "мягкое билинейное. Доли миллисекунды. Сразу."));

    ImGui::SeparatorText(T("Anti-aliasing", "Сглаживание"));
    const char* upscalers[] = {T("Off (the game's own)", "Выкл (сглаживание игры)"), "FSR 3.1",
                               "FSR 4 (INT8)", "FSR 4.1.1 (INT8)", "TAA", "DLSS (NVIDIA RTX)"};
    const char* unsupported = T("— not supported by this GPU", "— не поддерживается видеокартой");
    int upscaler = s.upscaler;
    if (ImGui::BeginCombo(T("Method", "Метод"), upscalers[upscaler])) {
        for (int i = 0; i < BbSettings::UpscalerCount; ++i) {
            const bool supported = i == BbSettings::UpscalerFsr4 ? s.fsr4_supported.load()
                : i == BbSettings::UpscalerFsr411 ? s.fsr411_supported.load()
                : i == BbSettings::UpscalerDlss   ? s.dlss_supported.load() : true;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(upscalers[i], i == upscaler)) {
                Store(s.upscaler, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", unsupported);
            }
        }
        ImGui::EndCombo();
    }
    Hint(T("Runs at the game's 1920x1080 on the scene before post-processing, with the game's own "
           "motion vectors, and replaces its edge smoothing. Changes apply on the next frame.",
           "Работает в родном 1920x1080 игры на сцене до постобработки, с векторами движения "
           "самой игры, и заменяет её сглаживание. Применяется со следующего кадра."));
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
                                "DLSS (NVIDIA RTX 40/50)"};
    const int frame_gen = s.frame_gen;
    if (ImGui::BeginCombo(T("Generation", "Генерация"), frame_gens[frame_gen])) {
        for (int i = 0; i < BbSettings::FrameGenCount; ++i) {
            const bool supported = i != BbSettings::FrameGenDlss || s.dlssg_max_multiplier > 1;
            ImGui::BeginDisabled(!supported);
            if (ImGui::Selectable(frame_gens[i], i == frame_gen)) {
                Store(s.frame_gen, i, true);
            }
            ImGui::EndDisabled();
            if (!supported) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", unsupported);
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
    const int max_multiplier = s.frame_gen == BbSettings::FrameGenDlss ? s.dlssg_max_multiplier.load() : 2;
    ImGui::BeginDisabled(s.frame_gen == BbSettings::FrameGenOff || max_multiplier <= 2);
    int multiplier = std::min(s.frame_gen_multiplier.load(), max_multiplier);
    if (ImGui::SliderInt(T("Multiplier", "Множитель"), &multiplier, 2, std::max(max_multiplier, 2), "%dx")) {
        Store(s.frame_gen_multiplier, multiplier, true);
    }
    ImGui::EndDisabled();
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
    ImGui::Text(T("%.0f FPS  %.1f ms  %s", "%.0f FPS  %.1f мс  %s"), frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f,
                frame_ms_avg,
                s.upscaler == BbSettings::UpscalerFsr3   ? "FSR 3.1"
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

#ifndef __ANDROID__
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
#else
// Android: the pad state the runtime reads from the app (BB_PAD_FILE), once per poll. The same
// controls as the SDL path: L3+R3 opens/closes the menu, then the D-pad, face buttons, L1/R1,
// Start and Back drive it.
bool HandlePad(u32 buttons) {
    static constexpr ImGuiKey keys[] = {
        ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceLeft,
        ImGuiKey_GamepadFaceUp,   ImGuiKey_GamepadDpadUp,    ImGuiKey_GamepadDpadDown,
        ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadL1,
        ImGuiKey_GamepadR1,       ImGuiKey_GamepadStart,     ImGuiKey_GamepadBack,
    };
    static u32 previous = 0;
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        previous = buttons;
        return false;
    }
    const u32 changed = buttons ^ previous;
    previous = buttons;
    const u32 sticks = BB_OVERLAY_PAD_L3 | BB_OVERLAY_PAD_R3;
    if ((changed & sticks) && (buttons & sticks) == sticks) {
        SetOpen(!menu_open);
        return true;
    }
    if (!menu_open) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    for (u32 i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
        if (changed & (1u << i)) {
            io.AddKeyEvent(keys[i], (buttons >> i) & 1);
        }
    }
    return true;
}
#endif // !__ANDROID__

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
void NotePresent() {
    const auto now = std::chrono::steady_clock::now();
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
