// bbport: the "window" of the Bionic game process: no SDL, no surface. See sdl_window.h.
#ifdef __ANDROID__
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "sdl_window.h"

// No mouse on Android (the app hands over pads only): the runtime's mouse controls stay idle.
extern "C" void bbgpu_mouse_configure(int) {}
extern "C" int bbgpu_mouse_sample(float* dx, float* dy, uint32_t* buttons, int* wheel) {
    *dx = *dy = 0.0f;
    *buttons = 0;
    *wheel = 0;
    return 0;
}

namespace Frontend {

namespace {
/// BB_SCREEN_W / BB_SCREEN_H: the app's screen in pixels, set by the launcher script; the size of the
/// AHB swapchain. Not both valid (320..16384): the fallback is 1920x1080.
bool ScreenFromEnv(s32& width, s32& height) {
    const char* w = std::getenv("BB_SCREEN_W");
    const char* h = std::getenv("BB_SCREEN_H");
    if (!w || !h) {
        return false;
    }
    const long wl = std::strtol(w, nullptr, 10);
    const long hl = std::strtol(h, nullptr, 10);
    if (wl < 320 || hl < 180 || wl > 16384 || hl > 16384) {
        return false;
    }
    width = static_cast<s32>(wl);
    height = static_cast<s32>(hl);
    return true;
}
} // namespace

WindowSDL::WindowSDL(s32, s32, const char*) : width{1920}, height{1080} {
    s32 w = 1920, h = 1080;
    const bool from_env = ScreenFromEnv(w, h);
    width = w;
    height = h;
    // Headless for the instance: no VK_KHR_*_surface extension, no surface (the AHB swapchain).
    window_info.type = WindowSystemType::Headless;
    std::printf("Output size %dx%d (%s)\n", w, h,
                from_env ? "BB_SCREEN_W/BB_SCREEN_H" : "default, BB_SCREEN_W/BB_SCREEN_H unset");
}

bool WindowSDL::PollEvents() {
    return true;
}

void WindowSDL::BeginTextInput(const std::string&, const std::string&) {}

int WindowSDL::PollTextInput(std::string&) {
    return 2;
}

} // namespace Frontend
#endif
