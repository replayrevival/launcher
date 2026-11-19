#pragma once
// Everything the launcher knows and decides, with no window, no RmlUi and no input devices: the profile, the settings
// (video / enhancement / controller pages), the remembered game, monitors and resolutions, the input map and the mods.
// `App` (the launcher screens) and the planned in-game overlay (docs/OVERLAY.md) are views of this one model.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/display_modes.h"
#include "core/input_map.h"
#include "core/mods.h"
#include "core/settings.h"
#include "core/state.h"

struct LauncherPaths {
    std::filesystem::path exeDir;   // where the launcher and its resources live
    std::filesystem::path dataDir;  // launcher state, game ini files, logs
};

// "fullscreen" (borderless, since 0.1.8) and "exclusive" both cover the monitor and list the monitor's reported sizes.
inline bool isFullscreenMode(const std::string& m) { return m == "fullscreen" || m == "exclusive"; }

class LauncherModel {
public:
    LauncherModel(Profile p, LauncherPaths pa) : profile(std::move(p)), paths(std::move(pa)) {}

    Profile profile;
    LauncherPaths paths;
    Settings settings;
    LauncherState state;
    InputMap input;
    ModStore mods;
    DisplayList disp;
    int selDisplay = 0;
    bool customForced = false;        // the Custom... resolution entry is chosen
    std::set<std::string> gameCaps;   // features the game reports (capabilities.json next to its exe, or LAUNCHER_GAME_CAPS); empty = none
    std::string gameVersion;          // version text of the game program ("0.1.0+c304"), "" = unknown
    std::string pendingNotice;        // plain-language message the view shows once it can
    std::string fakeDisplays;         // testing: a JSON monitor list instead of the real one
    bool ignoreSaved = false;         // testing: behave as a first start (the saved game is not read)

    // ---- locations
    std::filesystem::path settingsDir() const { return paths.dataDir / "game_settings"; }
    std::filesystem::path statePath() const { return paths.dataDir / "launcher_state.json"; }
    std::filesystem::path modsRoot() const { return paths.dataDir / profile.modsDir; }
    std::filesystem::path inputMapPath() const { return paths.dataDir / profile.inputMapFile; }
    std::filesystem::path exePath() const;  // LAUNCHER_GAME_EXE overrides the profile's program

    // ---- loading and saving
    // Reads the schemas and all saved files, applies the first-run preset and the V-Sync default, loads the mods.
    // (Monitors are looked at separately with refreshDisplays(), once a window exists.)
    bool load(std::string& err);
    void loadCapabilities();
    bool saveState(std::string& err);
    bool saveSettings(std::string& err);

    // ---- game build facts
    std::string buildNumber() const;       // "build 0.1.3+c304", "Game program not found", "build unknown"
    bool vsyncFixPresent() const;          // the game build has the V-Sync / variable-refresh start-up fix
    std::string buttonPrompts() const;     // "auto" | "xbox" | "xbox_original" | "playstation"
    void applyPresetNoted(const std::string& id);

    // ---- monitors and resolutions
    using DisplayDetector = std::function<DisplayList()>;
    // Looks at the monitors again and checks the saved display and resolution against them. A saved choice that
    // no longer exists falls back to the native resolution and leaves a notice in `pendingNotice`.
    void refreshDisplays(const DisplayDetector& detect);
    const DisplayInfo& curDisplay() const {
        static const DisplayInfo none{"Display 1", {1920, 1080, 60.f}, 1.f, {}};  // before refreshDisplays(): never index an empty list
        return disp.displays.empty() ? none : disp.displays[size_t(std::min<size_t>(size_t(selDisplay), disp.displays.size() - 1))];
    }
    std::string videoMode() const;
    std::vector<ResOption> resOptions() const;
    bool resValid(int w, int h) const;
    void setNative();

    // ---- changing a row
    struct StepOutcome {
        bool uiSizeChanged = false;
        std::string saveError;  // non-empty when a file could not be written
    };
    // Steps one settings row by `dir` (+1 / -1) and saves. `nowTicks` (ms) lets held buttons accelerate the custom sizes.
    StepOutcome stepSetting(const std::string& id, int dir, uint64_t nowTicks);
    // The settings-row actions the page markup produces (views/rows): "step" (arg "<row id>@<-1|1>", the chevrons) and
    // "cycle" (arg "<row id>", clicking the value steps forward). Returns false when `action` is not one of them. The launcher
    // and the overlay both route their clicks through this, so a row behaves the same in both.
    bool rowAction(const std::string& action, const std::string& arg, uint64_t nowTicks, StepOutcome& out);

private:
    std::string stepLastId_;
    uint64_t stepLastTick_ = 0;
    int stepStreak_ = 0;
};
