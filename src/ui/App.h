#pragma once
#include <RmlUi/Core.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/display_modes.h"
#include "core/identify.h"
#include "core/input_map.h"
#include "core/launcher_model.h"
#include "core/mod_patch.h"
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
    using Paths = LauncherPaths;

    // The launcher screens are a view of `LauncherModel` (src/core/launcher_model.h). The model's fields are
    // reachable under their old member names below so the screen code reads the same as before the split.
    App(Profile profile, Paths paths, Navigation& nav, SDL_Window* window)
        : model_(std::move(profile), std::move(paths)), nav_(nav), window_(window) {}
    LauncherModel& model() { return model_; }
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
    void useFakeDisplays(const std::string& jsonPath) { model_.fakeDisplays = jsonPath; }  // testing; call before init()
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
    std::string buildNumber() const { return model_.buildNumber(); }
    bool vsyncFixPresent() const { return model_.vsyncFixPresent(); }
    std::string buttonPromptsValue() const { return model_.buttonPrompts(); }  // "auto" | "xbox" | "xbox_original" | "playstation"
    void applyPresetNoted(const std::string& id) { model_.applyPresetNoted(id); }

    // video tab: monitors, resolution, option rows
    static std::string rowHtml(const std::string& id, const std::string& label, const std::string& hint, const std::string& val, bool on,
                               bool chevrons);
    std::string renderSpecialRow(const SettingRow& row);
    void refreshDisplays();
    const DisplayInfo& curDisplay() const { return model_.curDisplay(); }
    std::vector<ResOption> resOptions() const { return model_.resOptions(); }
    std::string videoMode() const { return model_.videoMode(); }
    bool resValid(int w, int h) const { return model_.resValid(w, h); }
    void setNative() { model_.setNative(); }
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
    std::filesystem::path inputMapPath() const { return model_.inputMapPath(); }

    // mods tab
    std::string renderMods();
    bool modsAct(const std::string& action, const std::string& arg);  // true when the action was a mods action
    void startModInstall(const std::filesystem::path& src, bool replace);
    void pollModInstall();
    // patch mods (mod_patch.md): states shown in the tab, and the "prepare" run that builds <mod>\_built before the game starts
    void refreshPatchStates();
    bool patchesNeedPrepare() const;
    void startPatchPrepare(bool thenPlay, bool original, bool borderless);
    void pollPatchPrepare();
    void reloadMods() { mods_.setRoot(modsRoot()); mods_.reload(); patchFailed_.clear(); refreshPatchStates(); }
    std::filesystem::path modsRoot() const { return model_.modsRoot(); }
    static void SDLCALL modDialogDone(void* ud, const char* const* list, int filter);

    // helpers
    std::filesystem::path exePath() const { return model_.exePath(); }
    std::filesystem::path settingsDir() const { return model_.settingsDir(); }
    std::string focusKey() const;
    void restoreFocus(const std::string& key);
    static std::string esc(const std::string& s) { return Rml::StringUtilities::EncodeRml(s); }
    static std::string shortPath(const std::string& p, size_t maxLen = 64);
    IdExpect expect() const { return {profile_.xbeSha256, profile_.titleHint}; }

    static void SDLCALL fileDialogDone(void* ud, const char* const* list, int filter);
    static void SDLCALL unpackDirDone(void* ud, const char* const* list, int filter);
    void takeDialogResult();

    LauncherModel model_;  // declared first: the reference aliases below are bound to its fields
    Profile& profile_ = model_.profile;
    Paths& paths_ = model_.paths;
    Settings& settings_ = model_.settings;
    LauncherState& state_ = model_.state;
    DisplayList& disp_ = model_.disp;
    int& selDisplay_ = model_.selDisplay;
    bool& customForced_ = model_.customForced;
    std::string& pendingNotice_ = model_.pendingNotice;
    std::string& gameVersion_ = model_.gameVersion;
    bool& ignoreSaved_ = model_.ignoreSaved;
    InputMap& input_ = model_.input;
    ModStore& mods_ = model_.mods;
    Navigation& nav_;
    SDL_Window* window_;
    Rml::ElementDocument* doc_ = nullptr;
    Rml::Element* screenEl_ = nullptr;


    Tab lastTab_ = Tab::Overview;      // tab of the last render, so the scroll position is kept only within one tab
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
    std::string lastPlayedPreset_;
    bool killOnExit_ = false;
    bool lastOriginal_ = false, lastBorderless_ = false;  // how the last game start was made
    std::filesystem::path lastVideoIni_;                 // the video ini that start used
    std::string gpuInfo_;
    std::function<void()> uiScaleChanged_;

    // mods
    std::future<ModInstallResult> modInstall_;
    bool modBusy_ = false;
    std::string modBusyName_, modsNote_;
    std::map<std::string, std::vector<ModPatchReport>> patchReports_;  // mod id -> state of each of its patches
    std::future<PatchRunResult> patchRun_;
    std::atomic<bool> patchCancel_{false};
    bool patchBusy_ = false, patchThenPlay_ = false, patchOrig_ = false, patchBorderless_ = false;
    std::set<std::string> patchFailed_;  // mods whose patches failed this session: PLAY does not try again (the Mods tab or PREPARE PATCHES does)
    bool skipPrepare_ = false;          // the next play() does not prepare again (it just did)
    std::filesystem::path modPending_;   // source waiting for the "replace it?" answer

    // modal message
    bool modalOpen_ = false;
    std::string modalDetails_;
};
