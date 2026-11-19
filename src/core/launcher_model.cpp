#include "core/launcher_model.h"

#include <cstdio>

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/identify.h"
#include "platform/ExeVersion.h"

namespace fs = std::filesystem;

namespace {

int toInt(const std::optional<std::string>& s) { return s ? std::atoi(s->c_str()) : 0; }

std::string sizeText(int w, int h) { return std::to_string(w) + " \xC3\x97 " + std::to_string(h); }

}  // namespace

fs::path LauncherModel::exePath() const {
    if (const char* o = std::getenv("LAUNCHER_GAME_EXE")) return pathFromUtf8(o);
    return paths.exeDir / pathFromUtf8(profile.exe);
}

// What the game says it can do. Hidden settings rows ("needs" in settings.json) appear only for features listed here.
// Source, in order: env LAUNCHER_GAME_CAPS="a,b" (tests), else <game exe folder>\capabilities.json {"features": ["hd_art"]}.
void LauncherModel::loadCapabilities() {
    gameCaps.clear();
    if (const char* e = std::getenv("LAUNCHER_GAME_CAPS")) {
        std::string cur;
        for (const char* c = e;; ++c) {
            if (*c == ',' || *c == 0) { if (!cur.empty()) gameCaps.insert(cur); cur.clear(); if (!*c) break; }
            else if (*c != ' ') cur += *c;
        }
    } else {
        std::ifstream f(exePath().parent_path() / "capabilities.json", std::ios::binary);
        if (f) {
            try {
                const auto j = nlohmann::json::parse(f);
                for (auto& v : j.value("features", nlohmann::json::array()))
                    if (v.is_string()) gameCaps.insert(v.get<std::string>());
            } catch (...) {}  // unreadable file = no extra features
        }
    }
    settings.setCapabilities(gameCaps);
}

// ---------------------------------------------------------------- load / save

bool LauncherModel::load(std::string& err) {
    if (!settings.loadSchema(paths.exeDir / "config" / "settings.json", err)) return false;

    settings.load(settingsDir());
    state.load(statePath());  // also holds the monitor choice, UI size, vrr note
    if (ignoreSaved) state.clearGame();
    gameVersion = readExeVersion(exePath());
    loadCapabilities();
    // While no game build with the V-Sync / variable-refresh fix is known, presets start with variable
    // refresh Off (V-Sync stays On), so the out-of-the-box settings avoid the known start-up crash.
    if (!vsyncFixPresent()) settings.setPresetDefault("video.vrr", "off");
    // Once a fixed build is detected, go back to Auto - but only if the launcher chose Off by itself and
    // the user has not touched the row since.
    if (vsyncFixPresent() && state.vrrDefaultedOff) {
        if (settings.value("video.vrr").value_or("auto") == "off") settings.set("video.vrr", std::nullopt);
        state.vrrDefaultedOff = false;
        std::string e3;
        settings.save(settingsDir(), e3);
        state.save(statePath(), e3);
    }
    // First run only: start from the MAX preset (the approved design shows it selected). "First run" means
    // there are no settings files AND the marker is absent; once the marker exists, saved choices are only
    // ever read, never replaced by a preset.
    std::error_code ec;
    const fs::path marker = paths.dataDir / "settings_initialized";
    const bool anyIni = fs::exists(settingsDir() / Settings::fileName("video"), ec) || fs::exists(settingsDir() / Settings::fileName("enhance"), ec);
    if (!anyIni && !fs::exists(marker, ec)) {
        applyPresetNoted("max");
        std::string e2;
        settings.save(settingsDir(), e2);
        state.firstStartNote = true;
        state.save(statePath(), e2);
    }
    if (!fs::exists(marker, ec)) {
        fs::create_directories(paths.dataDir, ec);
        std::ofstream(marker) << "settings initialised\n";
    }
    mods.setRoot(modsRoot());
    mods.reload();

    if (!input.loadSchema(pathToUtf8(paths.exeDir / "config" / "controls.json"), err)) return false;
    input.load(pathToUtf8(inputMapPath()));
    return true;
}

bool LauncherModel::saveState(std::string& err) { return state.save(statePath(), err); }

bool LauncherModel::saveSettings(std::string& err) { return settings.save(settingsDir(), err); }

// ---------------------------------------------------------------- game build facts

std::string LauncherModel::buildNumber() const {
    std::error_code ec;
    if (!fs::exists(exePath(), ec)) return "Game program not found";
    if (gameVersion.empty()) return "build unknown";
    // "<launcher version>+<game build tag>", e.g. 0.1.1+c250 (the tag is the part after "+" in the game's own version)
    const size_t plus = gameVersion.find('+');
    if (plus == std::string::npos) return "build " + gameVersion;
    return std::string("build ") + LAUNCHER_VERSION + gameVersion.substr(plus);
}

// True when the installed game build is known to contain the V-Sync / variable-refresh start-up fix
// (profile.json compat.vsync_vrr_fix_build; version looks like "0.1.0+c250").
bool LauncherModel::vsyncFixPresent() const {
    if (profile.vsyncVrrFixBuild <= 0) return false;
    const size_t c = gameVersion.rfind("+c");
    return c != std::string::npos && std::atoi(gameVersion.c_str() + c + 2) >= profile.vsyncVrrFixBuild;
}

std::string LauncherModel::buttonPrompts() const {
    const auto v = settings.value("enhance.button_prompts");
    return v && !v->empty() ? *v : "auto";
}

// Applies a preset and remembers whether variable refresh Off came from the launcher's own default
// (not from the user), so it can be switched back to Auto once the game build is fixed.
void LauncherModel::applyPresetNoted(const std::string& id) {
    settings.applyPreset(id);
    state.vrrDefaultedOff = settings.hasPresetDefault("video.vrr") && settings.value("video.vrr").value_or("auto") == "off";
    if (!ignoreSaved) {
        std::string err;
        saveState(err);
    }
}

// ---------------------------------------------------------------- monitors

std::string LauncherModel::videoMode() const { return settings.value("video.mode").value_or("window"); }

std::vector<ResOption> LauncherModel::resOptions() const { return buildResolutionOptions(curDisplay(), videoMode()); }

// Fullscreen only accepts what the display reports; windows may be any size that fits the desktop.
bool LauncherModel::resValid(int w, int h) const {
    const DisplayInfo& d = curDisplay();
    if (inOptions(resOptions(), w, h)) return true;
    return !isFullscreenMode(videoMode()) && w >= 320 && h >= 240 && w <= d.desktop.w && h <= d.desktop.h;
}

void LauncherModel::setNative() {
    const DisplayInfo& d = curDisplay();
    settings.set("video.width", std::to_string(d.desktop.w));
    settings.set("video.height", std::to_string(d.desktop.h));
    customForced = false;
}

void LauncherModel::refreshDisplays(const DisplayDetector& detect) {
    DisplayList list;
    std::string err;
    if (!fakeDisplays.empty()) {
        if (!loadFakeDisplays(fakeDisplays, list, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            list = {};
        }
    }
    if (list.displays.empty() && detect) list = detect();
    if (list.displays.empty()) {  // nothing found: keep the UI working with a plain 1080p screen
        DisplayInfo d;
        d.name = "Display";
        d.desktop = {1920, 1080, 60};
        list.displays.push_back(d);
        list.windowDisplay = 0;
    }
    disp = std::move(list);

    const std::string text = describeDisplays(disp);
    std::fprintf(stderr, "Displays detected:\n%s\n", text.c_str());
    {
        std::error_code ec;
        fs::create_directories(paths.dataDir / "logs", ec);
        std::ofstream f(paths.dataDir / "logs" / "displays.log", std::ios::trunc);
        f << text;
    }

    const int count = int(disp.displays.size());
    std::string notice;
    // The monitor the user picked, else the one the launcher window is on.
    int sel = state.displayChoice >= 0 ? state.displayChoice : disp.windowDisplay;
    if (sel < 0 || sel >= count) {
        sel = std::clamp(disp.windowDisplay, 0, count - 1);
        notice = "The monitor you chose before is not connected. The game will use \"" + disp.displays[size_t(sel)].name + "\".";
        state.displayChoice = -1;
        std::string e;
        saveState(e);
        settings.set("video.width", std::nullopt);  // forces the native fallback below
    }
    selDisplay = sel;
    settings.set("video.display", std::to_string(sel));

    const int w = toInt(settings.value("video.width")), h = toInt(settings.value("video.height"));
    if (w <= 0 || h <= 0) {
        setNative();  // first start (or just changed monitor): native is the default
    } else if (!resValid(w, h)) {
        setNative();
        const DisplayInfo& d = curDisplay();
        const std::string why = "Your saved resolution (" + sizeText(w, h) + ") is not available on this monitor any more. ";
        notice = (notice.empty() ? "" : notice + "\n") + why + "The game will use your screen's own resolution (" +
                 sizeText(d.desktop.w, d.desktop.h) + ").";
    }
    if (!notice.empty()) pendingNotice = notice;
    std::string e2;
    settings.save(settingsDir(), e2);
}

// ---------------------------------------------------------------- rows

LauncherModel::StepOutcome LauncherModel::stepSetting(const std::string& id, int dir, uint64_t nowTicks) {
    StepOutcome out;
    auto saveStateNoted = [&] {
        std::string e;
        if (!saveState(e)) out.saveError = e;
    };
    if (id == "update-check") {
        state.updateCheck = !state.updateCheck;
        saveStateNoted();
        return out;
    }
    if (id == "ui-size") {
        static const int sizes[] = {85, 100, 115, 130, 150};
        int i = 1;
        for (int k = 0; k < 5; ++k)
            if (sizes[k] == state.uiSize) i = k;
        state.uiSize = sizes[((i + dir) % 5 + 5) % 5];
        saveStateNoted();
        out.uiSizeChanged = true;
        return out;
    }
    if (id == "vrr" && state.vrrDefaultedOff) {  // the user now owns this setting
        state.vrrDefaultedOff = false;
        saveStateNoted();
    }
    if (id == "display") {
        const int n = int(disp.displays.size());
        selDisplay = ((selDisplay + dir) % n + n) % n;
        settings.set("video.display", std::to_string(selDisplay));
        state.displayChoice = selDisplay;
        saveStateNoted();
        const int w = toInt(settings.value("video.width")), h = toInt(settings.value("video.height"));
        if (!resValid(w, h)) setNative();
    } else if (id == "res") {
        const auto opts = resOptions();
        const int w = toInt(settings.value("video.width")), h = toInt(settings.value("video.height"));
        const bool custom = !isFullscreenMode(videoMode());  // fullscreen has no Custom... entry
        const int n = int(opts.size()) + (custom ? 1 : 0);  // the last entry is Custom...
        int idx = custom ? n - 1 : 0;
        if (!customForced || !custom)
            for (int i = 0; i < int(opts.size()); ++i)
                if (opts[size_t(i)].w == w && opts[size_t(i)].h == h) idx = i;
        idx = ((idx + dir) % n + n) % n;
        if (custom && idx == n - 1) {
            customForced = true;
        } else {
            customForced = false;
            settings.set("video.width", std::to_string(opts[size_t(idx)].w));
            settings.set("video.height", std::to_string(opts[size_t(idx)].h));
        }
    } else if (id == "res-w" || id == "res-h") {
        // Holding the button repeats; the step grows the longer it is held.
        stepStreak_ = (id == stepLastId_ && nowTicks - stepLastTick_ < 400) ? stepStreak_ + 1 : 0;
        stepLastId_ = id;
        stepLastTick_ = nowTicks;
        const int step = 10 * std::min(16, 1 + stepStreak_ / 2);
        const std::string key = id == "res-w" ? "video.width" : "video.height";
        const int v = std::clamp(toInt(settings.value(key)) + dir * step, 320, 16384);
        settings.set(key, std::to_string(v));
        customForced = true;
    } else {
        for (const char* page : {"video", "enhance", "controls"})
            for (auto& row : settings.page(page))
                if (row.id == id && row.special.empty()) { settings.cycle(row, dir); goto done; }
    }
done:
    if (id == "mode") {  // the list of sizes depends on the window mode (fullscreen: only reported modes)
        const int w = toInt(settings.value("video.width")), h = toInt(settings.value("video.height"));
        if (!resValid(w, h)) setNative();
        else if (isFullscreenMode(videoMode())) customForced = false;
    }
    std::string e;
    if (!saveSettings(e)) out.saveError = e;
    return out;
}

bool LauncherModel::rowAction(const std::string& action, const std::string& arg, uint64_t nowTicks, StepOutcome& out) {
    if (action == "step") {
        const size_t at = arg.rfind('@');
        if (at == std::string::npos) return true;  // ours, but malformed: nothing to do
        out = stepSetting(arg.substr(0, at), std::atoi(arg.c_str() + at + 1) < 0 ? -1 : +1, nowTicks);
        return true;
    }
    if (action == "cycle") {
        out = stepSetting(arg, +1, nowTicks);
        return true;
    }
    return false;
}
