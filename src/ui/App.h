#pragma once
#include <RmlUi/Core.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/display_modes.h"
#include "core/identify.h"
#include "core/input_map.h"
#include "core/mods.h"
#include "core/settings.h"
#include "core/state.h"
#include "core/xdvdfs.h"
#include "platform/DriveSearch.h"
#include "platform/GameProcess.h"
#include "ui/Navigation.h"

struct SDL_Window;

// The launcher's screens: first-start game picker, and the main screen with its tabs.
// Content is generated as RML into #screen; actions come back through data-action attributes.
class App : public Rml::EventListener {
public:
    struct Paths {
        std::filesystem::path exeDir;   // where the launcher and its resources live
        std::filesystem::path dataDir;  // launcher state, game ini files, logs
    };

    App(Profile profile, Paths paths, Navigation& nav, SDL_Window* window)
        : profile_(std::move(profile)), paths_(std::move(paths)), nav_(nav), window_(window) {}
    ~App() override;

    bool init(Rml::ElementDocument* doc, std::string& err);
    void update();                                // once per frame
    bool handleAction(NavAction a);               // actions Navigation did not handle itself
    void onDrop(const std::string& utf8Path);     // a file or folder dropped on the window
    void refreshHints();
    void killGameOnExit(bool v) { killOnExit_ = v; }
    void setGpuInfo(const std::string& s) { gpuInfo_ = s; }
    void setUiScaleListener(std::function<void()> f) { uiScaleChanged_ = std::move(f); }
    float uiScale() const;                       // user's "UI size" multiplier (1.0 = normal)
    void useFakeDisplays(const std::string& jsonPath) { fakeDisplays_ = jsonPath; }  // testing; call before init()
    // Test helpers: scroll offset of the option list, the focused row's key and a row's shown value.
    float pageScroll() const;
    std::string focusedKey() const { return focusKey(); }
    std::string rowValue(const std::string& key) const;
    bool elementCenter(const std::string& key, const std::string& part, float& x, float& y) const;
    void ignoreSavedGame(bool v) { ignoreSaved_ = v; }  // testing: behave as a first start (the saved file is not touched)
    bool gameRunning() const { return game_.running(); }
    bool busy() const { return checking_ || search_.running(); }

    void ProcessEvent(Rml::Event& ev) override;

private:
    enum class Screen { Setup, Main };
    enum class SetupStep { Choose, Searching, Result, Unpack, Unpacking };
    enum class Tab { Overview, Video, Enhance, Mods, Controllers };

    // actions
    void act(const std::string& action, const std::string& arg);
    void beginCheck(const std::filesystem::path& p);
    void acceptGame();
    void acceptFolder(const std::filesystem::path& folder, const IdResult& r, const std::filesystem::path& sourceImage);

    // disc image -> game folder (the launcher unpacks it once; the game only ever sees a folder)
    void enterUnpack();
    bool reuseUnpacked();
    void checkUnpackTarget();
    void startUnpack();
    void cancelUnpack();
    void pollUnpack();
    void afterUnpack();
    std::filesystem::path defaultUnpackTarget() const;
    void setTab(Tab t);
    // original: a one-off start with a clean video/enhancement ini (game defaults); the saved settings are not touched.
    // borderless: a one-off start with the window mode forced to borderless.
    void play(bool original = false, bool borderless = false);
    void onGameExit(int code, double secs);
    std::string supportDetails(int code, double secs) const;
    struct ModalButton { std::string label, action, arg; };
    // `copyText` is what COPY DETAILS copies; `shown` is the short text drawn in the box.
    void showModalEx(const std::string& title, const std::string& body, const std::string& shown, const std::string& copyText,
                     const std::vector<ModalButton>& buttons);
    void showModal(const std::string& title, const std::string& body, const std::string& details) {
        showModalEx(title, body, details, details, {});
    }
    void closeModal();
    bool saveSettings();
    bool saveState();

    // rendering
    void render();
    std::string renderSetup();
    std::string renderMain();
    std::string renderOverview();
    std::string renderRows(const std::string& page);
    void renderHeader();
    std::string buildNumber() const;
    bool vsyncFixPresent() const;
    std::string buttonPromptsValue() const;  // "auto" | "xbox" | "xbox_original" | "playstation"
    void applyPresetNoted(const std::string& id);

    // video tab: monitors, resolution, option rows
    static std::string rowHtml(const std::string& id, const std::string& label, const std::string& hint, const std::string& val, bool on,
                               bool chevrons);
    std::string renderSpecialRow(const SettingRow& row);
    void refreshDisplays();
    const DisplayInfo& curDisplay() const;
    std::vector<ResOption> resOptions() const;
    std::string videoMode() const;
    bool resValid(int w, int h) const;
    void setNative();
    void stepRow(const std::string& id, int dir);

    // controllers tab
    struct PortInfo {
        ResolvedPort r;                 // what the port resolved to (empty = nothing attached)
        SDL_JoystickID padId = 0;
        PadFamily family = PadFamily::Xbox;
    };
    struct DevOpt { std::string id, label; };
    std::string renderControllers();
    std::array<PortInfo, InputMap::kPorts> resolvePorts();
    std::vector<DevOpt> deviceOptions(const std::array<PortInfo, InputMap::kPorts>& info);
    std::string profileIdOf(const std::array<PortInfo, InputMap::kPorts>& info, int port) const;
    void adjustControl(const std::string& what, const std::string& arg, int delta);
    void beginListen(const std::string& actionId);
    void endListen();
    bool onCapture(const SDL_Event& ev);
    bool saveInput();
    std::filesystem::path dumpDir() const { return paths_.dataDir / "logs" / "dumps"; }
    std::filesystem::path inputMapPath() const { return paths_.dataDir / profile_.inputMapFile; }

    // mods tab
    std::string renderMods();
    bool modsAct(const std::string& action, const std::string& arg);  // true when the action was a mods action
    void startModInstall(const std::filesystem::path& src, bool replace);
    void pollModInstall();
    std::filesystem::path modsRoot() const { return paths_.dataDir / profile_.modsDir; }
    static void SDLCALL modDialogDone(void* ud, const char* const* list, int filter);

    // helpers
    std::filesystem::path exePath() const;
    std::filesystem::path settingsDir() const { return paths_.dataDir / "game_settings"; }
    std::string focusKey() const;
    void restoreFocus(const std::string& key);
    static std::string esc(const std::string& s) { return Rml::StringUtilities::EncodeRml(s); }
    static std::string shortPath(const std::string& p, size_t maxLen = 64);
    IdExpect expect() const { return {profile_.xbeSha256, profile_.titleHint}; }

    static void SDLCALL fileDialogDone(void* ud, const char* const* list, int filter);
    static void SDLCALL unpackDirDone(void* ud, const char* const* list, int filter);
    void takeDialogResult();

    Profile profile_;
    Paths paths_;
    Navigation& nav_;
    SDL_Window* window_;
    Rml::ElementDocument* doc_ = nullptr;
    Rml::Element* screenEl_ = nullptr;

    Settings settings_;
    LauncherState state_;
    DisplayList disp_;
    int selDisplay_ = 0;
    bool customForced_ = false;        // the Custom... resolution entry is chosen
    std::string pendingNotice_;        // shown as a message box once the main screen is up
    std::string fakeDisplays_;         // testing: a JSON monitor list instead of the real one
    std::string stepLastId_;
    uint64_t stepLastTick_ = 0;
    int stepStreak_ = 0;
    Tab lastTab_ = Tab::Overview;      // tab of the last render, so the scroll position is kept only within one tab
    InputMap input_;
    int ctlPort_ = 0;
    std::string listenAction_;      // control waiting for a button press ("" = not listening)
    std::string captured_;          // source captured by the listener, applied in update()
    bool listenCancelled_ = false;
    uint64_t listenSince_ = 0;      // SDL_GetTicks() when listening began

    Screen screen_ = Screen::Setup;
    SetupStep step_ = SetupStep::Choose;
    Tab tab_ = Tab::Overview;
    std::string lastScope_;        // which screen/step the last render was for; focus is only kept within one
    bool canCancelSetup_ = false;  // reached from "Change game", so Back returns to the main screen

    // game check (runs off the UI thread)
    std::future<IdResult> check_;
    bool checking_ = false;
    bool startupCheck_ = false;  // re-checking the remembered game at start-up
    std::filesystem::path checkPath_;
    IdResult result_;
    std::filesystem::path resultPath_;
    std::string setupNote_;  // plain-language message shown on the choose step

    // drive search
    DriveSearch search_;
    std::vector<FoundGame> found_;
    std::string searchCurrent_;
    bool searchWasRunning_ = false;

    // native dialog result handoff
    std::mutex dlgMu_;
    std::string dlgPath_;
    std::string dlgMod_;      // zip or folder chosen for "Install a mod"
    std::string dlgParent_;   // folder chosen for "Unpack to" (the game goes into <folder>\STE)

    // unpacking a disc image
    std::filesystem::path unpackImage_, unpackTarget_;
    bool unpackTargetChosen_ = false;      // the user changed the default folder
    uint64_t unpackBytes_ = 0;
    uint32_t unpackFiles_ = 0;
    std::string unpackNote_;               // plain-language problem or result shown on the unpack step
    bool unpackFits_ = false;
    bool unpacking_ = false;
    UnpackProgress prog_;
    UnpackResult unpackRes_;
    std::thread unpackThread_;
    std::atomic<bool> unpackDone_{false};
    std::string lastBarText_;

    // running game
    GameProcess game_;
    std::string gameVersion_;
    std::string lastPlayedPreset_;
    bool killOnExit_ = false;
    bool ignoreSaved_ = false;
    bool lastOriginal_ = false, lastBorderless_ = false;  // how the last game start was made
    std::filesystem::path lastVideoIni_;                 // the video ini that start used
    std::string gpuInfo_;
    std::function<void()> uiScaleChanged_;

    // mods
    ModStore mods_;
    std::future<ModInstallResult> modInstall_;
    bool modBusy_ = false;
    std::string modBusyName_, modsNote_;
    std::filesystem::path modPending_;   // source waiting for the "replace it?" answer

    // modal message
    bool modalOpen_ = false;
    std::string modalDetails_;
};
