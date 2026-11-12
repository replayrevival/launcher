#pragma once
// Settings rows as RmlUi markup, built from a LauncherModel. Shared by the launcher screens (App) and the planned
// in-game overlay (docs/OVERLAY.md). No window, no SDL, no input handling: the actions are `data-action` attributes
// ("cycle", "step") that the host turns into LauncherModel::stepSetting().
#include <string>

#include "core/launcher_model.h"

namespace views {

std::string esc(const std::string& s);  // RML-escape plain text

// A controller button picture (<glyph dir>/<stem>.png, Kenney Input Prompts, CC0). The host sets the folder once
// (the launcher: <exe dir>/assets/glyphs); generated markup uses the absolute path so it works from any document.
void setGlyphDir(const std::string& utf8Dir);
std::string glyphHtml(const std::string& stem);

// One option row. `hint` has one line per '\n'; a line starting with '!' is shown as a warning. `chevrons` adds the
// separate left/right click targets (the row value is a list, not a toggle).
std::string rowHtml(const std::string& id, const std::string& label, const std::string& hint, const std::string& val, bool on, bool chevrons);

// All rows of a settings page ("video", "enhance", "controls"), including the monitor and resolution rows.
std::string settingsRows(const LauncherModel& model, const std::string& page);

}  // namespace views
