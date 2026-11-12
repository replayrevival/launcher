// Video tab helpers: monitors and resolutions (detected at every start), the shared option-row markup
// with separate left/right click targets, and stepping a row from mouse, keyboard or pad.
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

// ---------------------------------------------------------------- markup

std::string App::rowHtml(const std::string& id, const std::string& label, const std::string& hint, const std::string& val, bool on,
                         bool chevrons) {
    std::string h = "<div class=\"setrow\"><div class=\"setlabel\"><div class=\"lbl\">" + esc(label) + "</div>";
    // One line per '\n'; a line starting with '!' is a warning (yellow).
    for (size_t pos = 0; pos < hint.size();) {
        size_t nl = hint.find('\n', pos);
        if (nl == std::string::npos) nl = hint.size();
        std::string line = hint.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) continue;
        const bool warn = line[0] == '!';
        if (warn) line.erase(0, 1);
        h += std::string("<div class=\"small ") + (warn ? "warnline\" style=\"color: #FFD43B" : "muted") + "\">" + esc(line) + "</div>";
    }
    h += "</div><button class=\"nav val" + std::string(on ? " on" : "") + "\" data-action=\"cycle\" data-arg=\"" + id + "\" data-key=\"row-" + id +
         "\" data-adjust=\"1\"><span class=\"marker\"/>";
    if (chevrons)
        h += "<span class=\"stepbtn\" data-action=\"step\" data-arg=\"" + id + "@-1\"><span class=\"chev left\"/></span>";
    h += "<span class=\"valtext\">" + esc(val) + "</span>";
    if (chevrons)
        h += "<span class=\"stepbtn\" data-action=\"step\" data-arg=\"" + id + "@1\"><span class=\"chev right\"/></span>";
    return h + "</button></div>";
}

// ---------------------------------------------------------------- monitors (the logic is in core/launcher_model.cpp)

void App::refreshDisplays() {
    model_.refreshDisplays([this] { return detectDisplays(window_); });
}
// ---------------------------------------------------------------- rows

std::string App::renderSpecialRow(const SettingRow& row) {
    std::string h;
    if (row.special == "display") {
        if (disp_.displays.size() < 2) return "";  // one monitor: nothing to choose
        const DisplayInfo& d = curDisplay();
        h += rowHtml("display", row.label, row.hint,
                     std::to_string(selDisplay_ + 1) + " \xC2\xB7 " + d.name + " \xC2\xB7 " + sizeText(d.desktop.w, d.desktop.h), false, true);
    } else if (row.special == "resolution") {
        const auto opts = resOptions();
        const int w = toInt(settings_.value("video.width")), h2 = toInt(settings_.value("video.height"));
        const bool fullscreen = videoMode() == "fullscreen";
        const bool custom = !fullscreen && (customForced_ || !inOptions(opts, w, h2));
        std::string val = "Custom\xE2\x80\xA6";
        for (auto& o : opts)
            if (!custom && o.w == w && o.h == h2) val = o.label;
        h += rowHtml("res", row.label, fullscreen ? "Fullscreen offers the sizes your monitor reports" : row.hint, val, false, true);
        if (custom) {
            h += rowHtml("res-w", "Custom width", "In pixels. Hold to go faster", std::to_string(w), false, true);
            h += rowHtml("res-h", "Custom height", "In pixels. Hold to go faster", std::to_string(h2), false, true);
        }
    }
    return h;
}

// Steps one row (mouse, keyboard or pad); the work is done by the model, then the screen is redrawn.
void App::stepRow(const std::string& id, int dir) {
    const auto r = model_.stepSetting(id, dir, SDL_GetTicks());
    if (r.uiSizeChanged && uiScaleChanged_) uiScaleChanged_();
    if (!r.saveError.empty()) showModal("Could not save", "Your choice could not be written.", r.saveError);
    render();
}