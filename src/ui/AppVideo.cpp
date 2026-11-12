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

// ---------------------------------------------------------------- monitors

const DisplayInfo& App::curDisplay() const { return disp_.displays[size_t(selDisplay_)]; }

std::string App::videoMode() const { return settings_.value("video.mode").value_or("window"); }

std::vector<ResOption> App::resOptions() const { return buildResolutionOptions(curDisplay(), videoMode()); }

// Fullscreen only accepts what the display reports; windows may be any size that fits the desktop.
bool App::resValid(int w, int h) const {
    const DisplayInfo& d = curDisplay();
    if (inOptions(resOptions(), w, h)) return true;
    return videoMode() != "fullscreen" && w >= 320 && h >= 240 && w <= d.desktop.w && h <= d.desktop.h;
}

void App::setNative() {
    const DisplayInfo& d = curDisplay();
    settings_.set("video.width", std::to_string(d.desktop.w));
    settings_.set("video.height", std::to_string(d.desktop.h));
    customForced_ = false;
}

// Looks at the monitors again and checks the saved display and resolution against them.
// A saved choice that no longer exists falls back to the native resolution, with a notice.
void App::refreshDisplays() {
    DisplayList list;
    std::string err;
    if (!fakeDisplays_.empty()) {
        if (!loadFakeDisplays(fakeDisplays_, list, err)) {
            SDL_Log("%s", err.c_str());
            list = {};
        }
    }
    if (list.displays.empty()) list = detectDisplays(window_);
    if (list.displays.empty()) {  // SDL found nothing: keep the UI working with a plain 1080p screen
        DisplayInfo d;
        d.name = "Display";
        d.desktop = {1920, 1080, 60};
        list.displays.push_back(d);
        list.windowDisplay = 0;
    }
    disp_ = std::move(list);

    const std::string text = describeDisplays(disp_);
    SDL_Log("Displays detected:\n%s", text.c_str());
    {
        std::error_code ec;
        fs::create_directories(paths_.dataDir / "logs", ec);
        std::ofstream f(paths_.dataDir / "logs" / "displays.log", std::ios::trunc);
        f << text;
    }

    const int count = int(disp_.displays.size());
    std::string notice;
    // The monitor the user picked, else the one the launcher window is on.
    int sel = state_.displayChoice >= 0 ? state_.displayChoice : disp_.windowDisplay;
    if (sel < 0 || sel >= count) {
        sel = std::clamp(disp_.windowDisplay, 0, count - 1);
        notice = "The monitor you chose before is not connected. The game will use \"" + disp_.displays[size_t(sel)].name + "\".";
        state_.displayChoice = -1;
        saveState();
        settings_.set("video.width", std::nullopt);  // forces the native fallback below
    }
    selDisplay_ = sel;
    settings_.set("video.display", std::to_string(sel));

    const int w = toInt(settings_.value("video.width")), h = toInt(settings_.value("video.height"));
    if (w <= 0 || h <= 0) {
        setNative();  // first start (or just changed monitor): native is the default
    } else if (!resValid(w, h)) {
        setNative();
        const DisplayInfo& d = curDisplay();
        const std::string why = "Your saved resolution (" + sizeText(w, h) + ") is not available on this monitor any more. ";
        notice = (notice.empty() ? "" : notice + "\n") + why + "The game will use your screen's own resolution (" +
                 sizeText(d.desktop.w, d.desktop.h) + ").";
    }
    if (!notice.empty()) pendingNotice_ = notice;
    std::string e2;
    settings_.save(settingsDir(), e2);
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

void App::stepRow(const std::string& id, int dir) {
    if (id == "ui-size") {
        static const int sizes[] = {85, 100, 115, 130, 150};
        int i = 1;
        for (int k = 0; k < 5; ++k)
            if (sizes[k] == state_.uiSize) i = k;
        state_.uiSize = sizes[((i + dir) % 5 + 5) % 5];
        saveState();
        if (uiScaleChanged_) uiScaleChanged_();
        render();
        return;
    }
    if (id == "vrr" && state_.vrrDefaultedOff) {  // the user now owns this setting
        state_.vrrDefaultedOff = false;
        saveState();
    }
    if (id == "display") {
        const int n = int(disp_.displays.size());
        selDisplay_ = ((selDisplay_ + dir) % n + n) % n;
        settings_.set("video.display", std::to_string(selDisplay_));
        state_.displayChoice = selDisplay_;
        saveState();
        const int w = toInt(settings_.value("video.width")), h = toInt(settings_.value("video.height"));
        if (!resValid(w, h)) setNative();
    } else if (id == "res") {
        const auto opts = resOptions();
        const int w = toInt(settings_.value("video.width")), h = toInt(settings_.value("video.height"));
        const bool custom = videoMode() != "fullscreen";  // fullscreen has no Custom... entry
        const int n = int(opts.size()) + (custom ? 1 : 0);  // the last entry is Custom...
        int idx = custom ? n - 1 : 0;
        if (!customForced_ || !custom)
            for (int i = 0; i < int(opts.size()); ++i)
                if (opts[size_t(i)].w == w && opts[size_t(i)].h == h) idx = i;
        idx = ((idx + dir) % n + n) % n;
        if (custom && idx == n - 1) {
            customForced_ = true;
        } else {
            customForced_ = false;
            settings_.set("video.width", std::to_string(opts[size_t(idx)].w));
            settings_.set("video.height", std::to_string(opts[size_t(idx)].h));
        }
    } else if (id == "res-w" || id == "res-h") {
        // Holding the button repeats; the step grows the longer it is held.
        const uint64_t now = SDL_GetTicks();
        stepStreak_ = (id == stepLastId_ && now - stepLastTick_ < 400) ? stepStreak_ + 1 : 0;
        stepLastId_ = id;
        stepLastTick_ = now;
        const int step = 10 * std::min(16, 1 + stepStreak_ / 2);
        const std::string key = id == "res-w" ? "video.width" : "video.height";
        const int v = std::clamp(toInt(settings_.value(key)) + dir * step, 320, 16384);
        settings_.set(key, std::to_string(v));
        customForced_ = true;
    } else {
        for (const char* page : {"video", "enhance", "controls"})
            for (auto& row : settings_.page(page))
                if (row.id == id && row.special.empty()) { settings_.cycle(row, dir); goto done; }
    }
done:
    if (id == "mode") {  // the list of sizes depends on the window mode (fullscreen: only reported modes)
        const int w = toInt(settings_.value("video.width")), h = toInt(settings_.value("video.height"));
        if (!resValid(w, h)) setNative();
        else if (videoMode() == "fullscreen") customForced_ = false;
    }
    saveSettings();
    render();
}
