#pragma once
#include "core/display_modes.h"

struct SDL_Window;

// Enumerates the monitors with SDL3 in physical pixels. `window` picks the default display (may be null).
DisplayList detectDisplays(SDL_Window* window);
