// Video tab helpers: detecting the monitors and stepping a row from mouse, keyboard or pad.
// (The logic is in core/launcher_model.cpp, the row markup in ui/views/rows.cpp.)
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include "core/identify.h"
#include "platform/Displays.h"
#include "ui/App.h"

namespace fs = std::filesystem;

namespace {

int toInt(const std::optional<std::string>& s) { return s ? std::atoi(s->c_str()) : 0; }

std::string sizeText(int w, int h) { return std::to_string(w) + " \xC3\x97 " + std::to_string(h); }

}  // namespace

// ---------------------------------------------------------------- monitors (the logic is in core/launcher_model.cpp)

void App::refreshDisplays() {
    model_.refreshDisplays([this] { return detectDisplays(window_); });
}
// Steps one row (mouse, keyboard or pad); the work is done by the model, then the screen is redrawn.
void App::stepRow(const std::string& id, int dir) { afterStep(model_.stepSetting(id, dir, SDL_GetTicks())); }

// What the launcher does after a row changed: UI size, a save failure box, redraw.
void App::afterStep(const LauncherModel::StepOutcome& r) {
    if (r.uiSizeChanged && uiScaleChanged_) uiScaleChanged_();
    if (!r.saveError.empty()) showModal("Could not save", "Your choice could not be written.", r.saveError);
    render();
}