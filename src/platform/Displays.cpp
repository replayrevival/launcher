#include "platform/Displays.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace {

// SDL reports sizes in screen coordinates; multiplying by the pixel density gives physical pixels
// (1.0 on Windows, where SDL is per-monitor DPI aware; 2.0 on a Retina Mac).
DisplayMode fromSdl(const SDL_DisplayMode& m) {
    const float d = m.pixel_density > 0 ? m.pixel_density : 1.f;
    DisplayMode out;
    out.w = int(std::lround(m.w * d));
    out.h = int(std::lround(m.h * d));
    out.hz = m.refresh_rate;
    return out;
}

}  // namespace

DisplayList detectDisplays(SDL_Window* window) {
    DisplayList out;
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    const SDL_DisplayID winDisplay = window ? SDL_GetDisplayForWindow(window) : 0;
    for (int i = 0; i < count; ++i) {
        DisplayInfo d;
        const char* n = SDL_GetDisplayName(ids[i]);
        d.name = n && *n ? n : "Display " + std::to_string(i + 1);
        if (const SDL_DisplayMode* dm = SDL_GetDesktopDisplayMode(ids[i])) d.desktop = fromSdl(*dm);
        d.scale = SDL_GetDisplayContentScale(ids[i]);
        if (d.scale <= 0) d.scale = 1.f;
        int n2 = 0;
        if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(ids[i], &n2)) {
            for (int k = 0; k < n2; ++k) d.modes.push_back(fromSdl(*modes[k]));
            SDL_free(modes);
        }
        if (d.desktop.w <= 0) {  // no desktop mode reported: use the biggest listed one
            for (auto& m : d.modes)
                if (m.w * m.h > d.desktop.w * d.desktop.h) d.desktop = m;
        }
        if (ids[i] == winDisplay) out.windowDisplay = i;
        out.displays.push_back(std::move(d));
    }
    SDL_free(ids);
    return out;
}
