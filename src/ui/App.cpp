#include "core/update_check.h"
#include "platform/UpdateFetch.h"
#include "ui/App.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "platform/ExeVersion.h"
#include "ui/views/rows.h"

namespace fs = std::filesystem;

namespace {

std::string nl2br(const std::string& escaped) {
    std::string out;
    for (char c : escaped) {
        if (c == '\n') out += "<br/>";
        else out += c;
    }
    return out;
}

std::string lowerStr(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

struct Hint {
    std::string key, text;
    bool yellow = false;
    std::vector<std::string> pics;  // button pictures (glyph stems); empty = text key cap
};

std::string gbText(uint64_t bytes) {
    char b[48];
    const double gb = double(bytes) / 1073741824.0;
    if (gb < 0.1) std::snprintf(b, sizeof b, "less than 0.1 GB");
    else std::snprintf(b, sizeof b, "%.1f GB", gb);
    return b;
}

}  // namespace

App::~App() {
    nav_.capture = nullptr;
    patchCancel_ = true;
    if (patchRun_.valid()) patchRun_.wait();
    search_.cancel();
    if (unpackThread_.joinable()) { prog_.cancel = true; unpackThread_.join(); }
    if (killOnExit_) game_.terminate();
}

// ---------------------------------------------------------------- setup

bool App::init(Rml::ElementDocument* doc, std::string& err) {
    doc_ = doc;
    screenEl_ = doc->GetElementById("screen");
    if (!screenEl_) { err = "The launcher screen file is missing its content area."; return false; }
    if (!model_.load(err)) return false;
    refreshDisplays();
    nav_.swapConfirmBack = input_.swapConfirmBack;
    nav_.capture = [this](const SDL_Event& ev) { return onCapture(ev); };

    doc->AddEventListener(Rml::EventId::Click, this);
    doc->AddEventListener(Rml::EventId::Mouseover, this);

    std::error_code ec;
    // Remembered game: trust it if the file is unchanged, otherwise check it again.
    if (!ignoreSaved_ && state_.load(paths_.dataDir / "launcher_state.json") && !state_.gamePath.empty()) {
        const fs::path p = pathFromUtf8(state_.gamePath);
        uint64_t size = 0;
        int64_t mtime = 0;
        statPath(p, size, mtime);
        if (fs::exists(p, ec) && !state_.sha256.empty() && size == state_.pathSize && mtime == state_.pathMtime &&
            (!fs::is_regular_file(p, ec) || (state_.gameKind == "disc image" && discImageOk()))) {
            screen_ = Screen::Main;
        } else {
            startupCheck_ = true;
            beginCheck(p);
        }
    }
    render();
    startUpdateCheck();
    return true;
}

std::string App::shortPath(const std::string& p, size_t maxLen) {
    if (p.size() <= maxLen) return p;
    return "..." + p.substr(p.size() - (maxLen - 3));
}

bool App::saveState() {
    std::string err;
    if (!model_.saveState(err)) {
        showModal("Could not save", "The launcher could not remember your game.", err);
        return false;
    }
    return true;
}

bool App::saveSettings() {
    std::string err;
    if (!model_.saveSettings(err)) {
        showModal("Could not save settings", "Your settings could not be written, so the game would not see them.", err);
        return false;
    }
    return true;
}
void App::beginCheck(const fs::path& p) {
    search_.cancel();
    checkPath_ = p;
    checking_ = true;
    result_ = {};
    const IdExpect ex = expect();
    check_ = std::async(std::launch::async, [p, ex] { return identifyGame(p, ex); });
    if (!startupCheck_) {
        screen_ = Screen::Setup;
        step_ = SetupStep::Result;
        render();
    }
}

void App::acceptGame() {
    if (result_.kind == "disc image") {
        if (discImageOk()) { acceptImage(resultPath_, result_); return; }   // the game mounts the image itself, read-only
        enterUnpack();   // an older game program only runs from a folder
        return;
    }
    acceptFolder(resultPath_, result_, fs::path());
}

void App::acceptImage(const fs::path& image, const IdResult& r) {
    state_.gamePath = pathToUtf8(image);
    state_.gameKind = "disc image";
    state_.gameTitle = r.title;
    state_.gameRegion = r.region;
    state_.sha256 = r.sha256;
    statPath(image, state_.pathSize, state_.pathMtime);
    state_.sourceImage.clear(); state_.sourceSize = 0; state_.sourceMtime = 0;
    if (!saveState()) return;
    screen_ = Screen::Main;
    tab_ = Tab::Overview;
    render();
}

void App::acceptFolder(const fs::path& folder, const IdResult& r, const fs::path& sourceImage) {
    state_.gamePath = pathToUtf8(folder);
    state_.gameKind = "folder";
    state_.gameTitle = r.title;
    state_.gameRegion = r.region;
    state_.sha256 = r.sha256;
    statPath(folder, state_.pathSize, state_.pathMtime);
    state_.sourceImage.clear(); state_.sourceSize = 0; state_.sourceMtime = 0;
    if (!sourceImage.empty()) {
        state_.sourceImage = pathToUtf8(sourceImage);
        statPath(sourceImage, state_.sourceSize, state_.sourceMtime);
    }
    if (!saveState()) return;
    screen_ = Screen::Main;
    tab_ = Tab::Overview;
    render();
}

// ---------------------------------------------------------------- disc image unpacking

fs::path App::defaultUnpackTarget() const { return paths_.dataDir / "game" / "STE"; }

// A valid unpacked copy of this image already exists (remembered from last time, or in the default folder): use it, no second unpack.
bool App::reuseUnpacked() {
    std::vector<fs::path> candidates;
    std::error_code ec;
    uint64_t size = 0; int64_t mtime = 0;
    statPath(unpackImage_, size, mtime);
    if (!state_.sourceImage.empty() && state_.sourceImage == pathToUtf8(unpackImage_) && state_.sourceSize == size && state_.sourceMtime == mtime &&
        state_.gameKind == "folder" && !state_.gamePath.empty())
        candidates.push_back(pathFromUtf8(state_.gamePath));
    candidates.push_back(defaultUnpackTarget());
    for (const auto& c : candidates) {
        if (!fs::is_directory(c, ec)) continue;
        const IdResult r = identifyGame(c, expect());
        if (r.status != IdStatus::Ok) continue;
        std::fprintf(stderr, "[launcher] reusing the unpacked copy at %s (no second unpack)\n", pathToUtf8(c).c_str());
        acceptFolder(c, r, unpackImage_);
        return true;
    }
    return false;
}

void App::checkUnpackTarget() {
    unpackNote_.clear();
    unpackFits_ = false;
    std::string err;
    std::vector<XdvdfsEntry> entries;
    unpackBytes_ = 0; unpackFiles_ = 0;
    if (!xdvdfsList(unpackImage_, entries, unpackBytes_, err)) { unpackNote_ = err; return; }
    for (auto& e : entries) if (!e.dir) ++unpackFiles_;
    unpackNote_ = unpackPreflight(unpackImage_, unpackTarget_, unpackBytes_);
    unpackFits_ = unpackNote_.empty();
}

void App::enterUnpack() {
    unpackImage_ = resultPath_;
    if (reuseUnpacked()) return;
    if (!unpackTargetChosen_) {
        unpackTarget_ = defaultUnpackTarget();
        std::error_code ec;
        for (int i = 2; fs::exists(unpackTarget_, ec) && i < 100; ++i)   // a folder is in the way (not a valid copy): pick the next free name
            unpackTarget_ = defaultUnpackTarget().parent_path() / ("STE-" + std::to_string(i));
    }
    checkUnpackTarget();
    screen_ = Screen::Setup;
    step_ = SetupStep::Unpack;
    render();
}

void App::startUnpack() {
    if (unpacking_ || !unpackFits_) return;
    prog_.cancel = false;
    prog_.bytesDone = 0; prog_.filesDone = 0; prog_.bytesTotal = unpackBytes_; prog_.filesTotal = unpackFiles_;
    prog_.throttleMs = 0;
    if (const char* t = SDL_getenv("LAUNCHER_UNPACK_THROTTLE_MS")) prog_.throttleMs = uint32_t(std::atoi(t));   // testing only
    unpackDone_ = false;
    unpacking_ = true;
    unpackNote_.clear();
    lastBarText_.clear();
    step_ = SetupStep::Unpacking;
    render();
    unpackThread_ = std::thread([this] {
        unpackRes_ = unpackImage(unpackImage_, unpackTarget_, prog_);
        unpackDone_ = true;
    });
}

void App::cancelUnpack() {
    if (!unpacking_) return;
    prog_.cancel = true;
    if (auto* e = doc_->GetElementById("unpack-text")) e->SetInnerRML("Cancelling...");
}

void App::pollUnpack() {
    if (!unpacking_) return;
    if (!unpackDone_) {
        const uint64_t total = std::max<uint64_t>(prog_.bytesTotal, 1), done = prog_.bytesDone;
        char txt[160];
        std::snprintf(txt, sizeof txt, "%u of %u files  -  %.2f of %.2f GB", prog_.filesDone.load(), prog_.filesTotal.load(),
                      double(done) / 1073741824.0, double(prog_.bytesTotal) / 1073741824.0);
        const std::string t = prog_.cancel ? std::string("Cancelling...") : std::string(txt);
        if (t != lastBarText_) {
            lastBarText_ = t;
            if (auto* e = doc_->GetElementById("unpack-text")) e->SetInnerRML(esc(t));
            if (auto* b = doc_->GetElementById("unpack-bar")) {   // the fill is a gradient on the track (a child element did not render)
                char g[160];
                const double pct = 100.0 * double(done) / double(total);
                std::snprintf(g, sizeof g, "linear-gradient(to right, #FFD43B %.1f%%, #00000000 %.1f%%)", pct, pct);
                b->SetProperty("decorator", g);
            }
        }
        return;
    }
    unpackThread_.join();
    unpacking_ = false;
    if (unpackRes_.ok) { afterUnpack(); return; }
    step_ = SetupStep::Unpack;
    checkUnpackTarget();   // refresh the numbers; the folder is clean again (the .partial is gone)
    unpackNote_ = unpackRes_.cancelled ? "Unpacking was cancelled. Nothing was kept, and your disc image was not changed. You can try again."
                                      : unpackRes_.error + " Nothing was kept.";
    render();
}

// Unpacked: check default.xbe and the known-good hash exactly like for a folder you pick, then use the folder from now on.
void App::afterUnpack() {
    const IdResult r = identifyGame(unpackTarget_, expect());
    if (r.status == IdStatus::Ok) { acceptFolder(unpackTarget_, r, unpackImage_); return; }
    result_ = r;
    resultPath_ = unpackTarget_;
    step_ = SetupStep::Result;
    render();
}

void SDLCALL App::unpackDirDone(void* ud, const char* const* list, int) {
    auto* self = static_cast<App*>(ud);
    if (!list || !list[0]) return;
    std::lock_guard<std::mutex> l(self->dlgMu_);
    self->dlgParent_ = list[0];
}

void App::onDrop(const std::string& utf8Path) {
    if (utf8Path.empty() || modalOpen_) return;
    if (screen_ == Screen::Main && !modsRoot().empty() && !profile_.modsDirEnv.empty() && ModStore::looksLikeMod(pathFromUtf8(utf8Path))) {
        tab_ = Tab::Mods;  // a mod, not a game: install it
        mods_.setRoot(modsRoot());
        reloadMods();
        startModInstall(pathFromUtf8(utf8Path), false);
        return;
    }
    canCancelSetup_ = !state_.gamePath.empty();
    beginCheck(pathFromUtf8(utf8Path));
}

void SDLCALL App::fileDialogDone(void* ud, const char* const* list, int) {
    auto* self = static_cast<App*>(ud);
    if (!list || !list[0]) return;  // error or cancelled
    std::lock_guard<std::mutex> l(self->dlgMu_);
    self->dlgPath_ = list[0];
}

void App::takeDialogResult() {
    std::string p;
    {
        std::lock_guard<std::mutex> l(dlgMu_);
        p.swap(dlgPath_);
    }
    if (!p.empty()) beginCheck(pathFromUtf8(p));
    std::string modSrc;
    {
        std::lock_guard<std::mutex> l(dlgMu_);
        modSrc.swap(dlgMod_);
    }
    if (!modSrc.empty() && screen_ == Screen::Main) startModInstall(pathFromUtf8(modSrc), false);
    std::string parent;
    {
        std::lock_guard<std::mutex> l(dlgMu_);
        parent.swap(dlgParent_);
    }
    if (!parent.empty() && screen_ == Screen::Setup && step_ == SetupStep::Unpack) {
        unpackTarget_ = pathFromUtf8(parent) / "STE";
        unpackTargetChosen_ = true;
        checkUnpackTarget();
        render();
    }
}

void App::setTab(Tab t) {
    tab_ = t;
    if (t == Tab::Mods) reloadMods();  // folders may have changed by hand
    render();
}

// ---------------------------------------------------------------- actions

void App::ProcessEvent(Rml::Event& ev) {
    Rml::Element* e = ev.GetTargetElement();
    while (e && !e->HasAttribute("data-action")) e = e->GetParentNode();
    if (!e) return;
    if (ev.GetId() == Rml::EventId::Mouseover) {
        while (e && !e->IsClassSet("nav")) e = e->GetParentNode();  // a step arrow focuses its whole row control
        if (e && !e->HasAttribute("disabled")) e->Focus();
        return;
    }
    act(e->GetAttribute<Rml::String>("data-action", ""), e->GetAttribute<Rml::String>("data-arg", ""));
}

void App::act(const std::string& action, const std::string& arg) {
    if (modalOpen_ && action != "modal-ok" && action != "copy-details" && action.rfind("modal-", 0) != 0) return;
    if (!listenAction_.empty() && action != "ctl-bind") endListen();

    if (action == "step" || action == "cycle") {  // option row: chevrons ("<row id>@<-1|1>") or a click on the value
        LauncherModel::StepOutcome r;
        if (model_.rowAction(action, arg, SDL_GetTicks(), r)) afterStep(r);
        return;
    }
    if (action == "ctl-step") {  // arg = "<kind>@<arg>@<dir>"
        const size_t at = arg.rfind('@'), at1 = arg.rfind('@', at - 1);
        if (at != std::string::npos && at1 != std::string::npos && at1 < at)
            adjustControl(arg.substr(0, at1), arg.substr(at1 + 1, at - at1 - 1), std::atoi(arg.c_str() + at + 1) < 0 ? -1 : +1);
        return;
    }
    if (action.rfind("ctl-", 0) == 0) {
        if (action == "ctl-port") {
            ctlPort_ = std::clamp(std::atoi(arg.c_str()), 0, InputMap::kPorts - 1);
            render();
        } else if (action == "ctl-bind") {
            if (listenAction_ == arg) { endListen(); render(); } else beginListen(arg);
        } else if (action == "ctl-reset") {
            const auto info = resolvePorts();
            const std::string pid = profileIdOf(info, ctlPort_);
            if (!pid.empty()) { input_.resetBindings(pid); saveInput(); }
            render();
        } else if (action == "ctl-swap") {
            input_.swapConfirmBack = !input_.swapConfirmBack;
            nav_.swapConfirmBack = input_.swapConfirmBack;
            saveInput();
            render();
        } else if (action == "ctl-test") {
            const auto info = resolvePorts();
            const std::string pid = profileIdOf(info, ctlPort_);
            const PortInfo& pi = info[ctlPort_];
            if (pi.padId == 0)
                showModal("No controller on this port", "Connect a controller, or choose the controller for this port, then try again.", "");
            else if (pid.empty() || input_.profile(pid).rumble == 0)
                showModal("Rumble is off", "Turn rumble on for this controller first.", "");
            else if (!nav_.rumble(pi.padId, input_.profile(pid).rumble / 100.f, 700))
                showModal("This controller cannot rumble", "The controller or its connection does not report vibration.", pi.r.name);
        } else {
            adjustControl(action, arg, +1);  // ctl-device / ctl-deadzone / ctl-rumble / ctl-stick by click
        }
        return;
    }

    if (browserAct(action, arg)) return;
    if (modsAct(action, arg)) return;
    if (action == "modal-ok") {
        closeModal();
    } else if (action == "modal-retry-original") {
        closeModal();
        play(true, false);
    } else if (action == "modal-retry-borderless") {
        closeModal();
        play(false, true);
    } else if (action == "modal-save-report") {
        saveCrashReport();
    } else if (action == "modal-open-logs") {
        std::error_code lec;
        const fs::path dir = paths_.dataDir / "logs";
        fs::create_directories(dir, lec);
        std::string url = pathToUtf8(fs::absolute(dir));
        std::replace(url.begin(), url.end(), '\\', '/');
        SDL_OpenURL(("file:///" + url).c_str());
    } else if (action == "copy-details") {
        SDL_SetClipboardText(modalDetails_.c_str());
    } else if (action == "choose-file") {
        openBrowser(BrowseFor::Game);
    } else if (action == "choose-folder") {
        openBrowser(BrowseFor::GameFolder);
    } else if (action == "unpack-folder") {
        openBrowser(BrowseFor::UnpackParent);
    } else if (action == "unpack") {
        startUnpack();
    } else if (action == "unpack-cancel") {
        cancelUnpack();
    } else if (action == "search") {
        screen_ = Screen::Setup;
        step_ = SetupStep::Searching;
        found_.clear();
        search_.start(expect());
        render();
    } else if (action == "pick-found") {
        const size_t i = size_t(std::atoi(arg.c_str()));
        if (i < found_.size()) beginCheck(found_[i].path);
    } else if (action == "accept") {
        acceptGame();
    } else if (action == "restart") {
        search_.cancel();
        setupNote_.clear();
        step_ = SetupStep::Choose;
        render();
    } else if (action == "tab") {
        static const struct { const char* n; Tab t; } tabs[] = {{"overview", Tab::Overview}, {"video", Tab::Video},
            {"enhance", Tab::Enhance}, {"mods", Tab::Mods}, {"controllers", Tab::Controllers}};
        for (auto& t : tabs)
            if (arg == t.n) setTab(t.t);
    } else if (action == "preset") {
        dismissFirstStartNote();
        applyPresetNoted(arg);
        saveSettings();
        render();
    } else if (action == "open-update") {
        if (isSafeReleaseUrl(state_.latestUrl)) SDL_OpenURL(state_.latestUrl.c_str());
    } else if (action == "change-game") {
        canCancelSetup_ = true;
        screen_ = Screen::Setup;
        step_ = SetupStep::Choose;
        setupNote_.clear();
        render();
    } else if (action == "play") {
        play();
    }
}

bool App::handleAction(NavAction a) {
    if (modalOpen_) {
        if (a == NavAction::Back) {
            if (browserOpen_) browserBack();
            else closeModal();
            return true;
        }
        return false;
    }
    Rml::Element* f = doc_->GetContext()->GetFocusElement();

    if ((a == NavAction::Left || a == NavAction::Right) && f && f->HasAttribute("data-adjust")) {
        const std::string id = f->GetAttribute<Rml::String>("data-arg", "");
        const std::string kind = f->GetAttribute<Rml::String>("data-adjust", "");
        if (kind.rfind("ctl-", 0) == 0) {
            adjustControl(kind, id, a == NavAction::Right ? +1 : -1);
            return true;
        }
        stepRow(id, a == NavAction::Right ? +1 : -1);
        return true;
    }

    if (screen_ == Screen::Setup) {
        if (a == NavAction::Extra1) { act("search", ""); return true; }
        if (a == NavAction::Back) {
            if (step_ == SetupStep::Unpacking) cancelUnpack();
            else if (step_ != SetupStep::Choose) act("restart", "");
            else if (canCancelSetup_ && !state_.gamePath.empty()) { screen_ = Screen::Main; render(); }
            return true;
        }
        return false;
    }

    // main screen
    const int n = 5;
    if (a == NavAction::PrevTab || a == NavAction::NextTab) {
        const int cur = int(tab_);
        setTab(Tab((cur + (a == NavAction::NextTab ? 1 : n - 1)) % n));
        return true;
    }
    if (a == NavAction::Back) {
        if (tab_ != Tab::Overview) setTab(Tab::Overview);
        return true;
    }
    if (a == NavAction::Play) { play(); return true; }
    return false;
}

// ---------------------------------------------------------------- launching

void App::play(bool original, bool borderless) {
    if (screen_ != Screen::Main || game_.running()) return;
    dismissFirstStartNote();
    refreshDisplays();  // monitors may have changed since the launcher started
    if (!pendingNotice_.empty()) { render(); return; }  // shows the notice; press PLAY again to start
    std::error_code ec0;
    const bool imageOk = playingFromImage() && discImageOk() && fs::is_regular_file(pathFromUtf8(state_.gamePath), ec0);
    if (!imageOk && !fs::is_directory(state_.gamePath.empty() ? fs::path() : pathFromUtf8(state_.gamePath))) {
        showModal("Your game needs to be a folder",
                  "The game runs from an unpacked folder. Choose your disc image or extracted game folder again; a disc image is unpacked once for you.",
                  state_.gamePath);
        return;
    }
    if (!saveSettings()) return;
    if (!skipPrepare_ && !patchBusy_ && patchesNeedPrepare()) { startPatchPrepare(true, original, borderless); return; }
    skipPrepare_ = false;

    LaunchSpec spec;
    spec.exe = exePath();
    spec.workDir = spec.exe.parent_path();
    if (const char* a = SDL_getenv("LAUNCHER_GAME_ARGS")) {  // testing only (with LAUNCHER_GAME_EXE): arguments for a stand-in program
        std::istringstream as(a);
        std::string tok;
        while (as >> tok) spec.args.push_back(tok);
    } else {
        // Always hand the game folder over on the command line as well as in the environment (the game wins on --game).
        spec.args.push_back("--game");
        spec.args.push_back(state_.gamePath);
    }
    spec.env = profile_.env;
    // Hidden developer toggle: the game opens its own console (--console, STE_CONSOLE=1) and the launcher does not hide it.
    // Normally the game runs with no console window at all.
    const char* ce = std::getenv("LAUNCHER_GAME_CONSOLE");
    spec.showConsole = gameConsole_ || state_.devGameConsole || (ce && *ce && std::string(ce) != "0");
    if (spec.showConsole) {
        spec.args.push_back("--console");
        spec.env["STE_CONSOLE"] = "1";
    }
    // A one-off "original settings" start uses its own clean ini files; the user's saved ones are never touched.
    fs::path iniDir = settingsDir();
    if (original) {
        iniDir = paths_.dataDir / "game_settings_retry";
        Settings clean = settings_;
        clean.clearAll();
        std::string cerr;
        if (!clean.save(iniDir, cerr)) { showModal("Could not prepare the start", "The original settings could not be written.", cerr); return; }
    }
    for (auto& [fileId, envName] : profile_.iniEnv)
        spec.env[envName] = pathToUtf8(fs::absolute(iniDir / Settings::fileName(fileId)));
    lastVideoIni_ = iniDir / Settings::fileName("video");
    lastOriginal_ = original;
    lastBorderless_ = borderless;
    if (borderless) spec.env["STE_VIDEO_MODE"] = "borderless";  // env override, the saved window mode stays as it is
    spec.env[profile_.gamePathEnv] = state_.gamePath;
    // The one-off "original settings" start also gets the player's prompt choice: it is an input preference, not a video setting.
    if (!profile_.modsDirEnv.empty()) {
        std::error_code mec;
        fs::create_directories(modsRoot(), mec);
        spec.env[profile_.modsDirEnv] = pathToUtf8(fs::absolute(modsRoot()));
    }
    if (!profile_.buttonPromptsEnv.empty()) spec.env[profile_.buttonPromptsEnv] = buttonPromptsValue();
    {  // the game writes [CRASH] lines and minidumps here
        std::error_code dec;
        fs::create_directories(dumpDir(), dec);
        spec.env["STE_DUMP_DIR"] = pathToUtf8(fs::absolute(dumpDir()));
    }
    if (!saveInput()) return;
    spec.env[profile_.inputMapEnv] = pathToUtf8(fs::absolute(inputMapPath()));
    lastPlayedPreset_ = original ? "original" : settings_.activePreset();
    for (auto& [k, v] : settings_.presetEnv(lastPlayedPreset_)) spec.env[k] = v;

    char stamp[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
    spec.logOut = paths_.dataDir / "logs" / (std::string("play_") + stamp + ".out");
    spec.logErr = paths_.dataDir / "logs" / (std::string("play_") + stamp + ".err");

    std::error_code ec;
    if (!fs::exists(spec.exe, ec)) {
        showModal("The game program is missing",
                  "The launcher cannot find the game program (" + pathToUtf8(spec.exe.filename()) + ") next to it. It may have been deleted by your virus scanner, "
                  "or the download was not fully unzipped. Unzip the download again into a fresh folder, and keep all its files together.",
                  pathToUtf8(spec.exe));
        return;
    }
    if (!fs::exists(state_.gamePath.empty() ? fs::path() : pathFromUtf8(state_.gamePath), ec)) {
        showModal("Your game file is missing",
                  "The launcher can no longer find your game file. It may have moved, or its drive is not connected. "
                  "Choose it again to carry on.",
                  state_.gamePath);
        return;
    }
    std::string err;
    if (!game_.start(spec, err)) {
        showModal("The game could not start", err, "Program: " + pathToUtf8(spec.exe));
        return;
    }
    renderHeader();
    render();
}

void App::update() {
    takeDialogResult();
    pollUnpack();
    pollModInstall();
    pollPatchPrepare();
    pollUpdateCheck();

    if (!listenAction_.empty()) {
        if (!captured_.empty() || listenCancelled_ || SDL_GetTicks() - listenSince_ > 6000) {
            const std::string src = captured_;
            const std::string actionId = listenAction_;
            endListen();
            if (!src.empty()) {
                const auto info = resolvePorts();
                const std::string pid = profileIdOf(info, ctlPort_);
                if (!pid.empty() && input_.assign(pid, actionId, src)) saveInput();
            }
            render();
        }
    }
    if (nav_.takePadsChanged() && screen_ == Screen::Main && tab_ == Tab::Controllers && !modalOpen_) render();

    if (checking_ && check_.valid() && check_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        result_ = check_.get();
        checking_ = false;
        resultPath_ = checkPath_;
        if (startupCheck_) {
            startupCheck_ = false;
            if (result_.status == IdStatus::Ok) {
                acceptGame();
            } else {
                canCancelSetup_ = false;
                screen_ = Screen::Setup;
                step_ = SetupStep::Result;
                render();
            }
        } else {
            render();
        }
    }

    if (search_.running() || (step_ == SetupStep::Searching && screen_ == Screen::Setup)) {
        std::vector<FoundGame> fresh;
        const std::string cur = search_.poll(fresh);
        if (!fresh.empty()) {
            for (auto& f : fresh) found_.push_back(std::move(f));
            // Verified copies first.
            std::stable_sort(found_.begin(), found_.end(), [](const FoundGame& a, const FoundGame& b) {
                return (a.id.status == IdStatus::Ok) > (b.id.status == IdStatus::Ok);
            });
            render();
        }
        if (cur != searchCurrent_) {
            searchCurrent_ = cur;
            if (auto* e = doc_->GetElementById("search-current")) e->SetInnerRML(esc(shortPath(cur)));
        }
        if (!search_.running() && searchWasRunning_ && screen_ == Screen::Setup && step_ == SetupStep::Searching) {
            searchCurrent_ = found_.empty() ? "No Xbox game was found on your drives. You can still choose the file yourself." : "";
            render();
        }
        searchWasRunning_ = search_.running();
    }

    int code = 0;
    if (game_.running() && game_.pollExited(code)) {
        const double secs = game_.secondsRunning();
        // Whatever happened in the game, our saved choices stay as they are; re-read the files so the
        // screen shows what is really on disk (an unreadable file keeps the values in memory).
        settings_.reload(settingsDir());
        saveSettings();
        renderHeader();
        // The overlay's "Return to launcher" is a normal end (profile exit_codes.return_to_launcher, 20): no "game stopped" box.
        if (code != profile_.returnToLauncherCode && (code != 0 || secs < 5.0)) onGameExit(code, secs);
        render();
    }
}

// ---------------------------------------------------------------- modal

void App::showModalEx(const std::string& title, const std::string& body, const std::string& shown, const std::string& copyText,
                      const std::vector<ModalButton>& buttons) {
    modalOpen_ = true;
    modalDetails_ = copyText;
    std::string h = "<div class=\"modal-back\"><div class=\"modal modal-panel\"><div class=\"title modal-title\">" + esc(title) +
                    "</div><p class=\"secondary\">" + nl2br(esc(body)) + "</p>";
    if (!shown.empty()) h += "<p class=\"mono muted\">" + nl2br(esc(shown)) + "</p>";
    const std::string ok = "<button class=\"nav default\" data-action=\"modal-ok\" data-key=\"modal-ok\"><span class=\"marker\"/>OK</button>";
    const std::string copy = "<button class=\"nav\" data-action=\"copy-details\" data-key=\"copy\"><span class=\"marker\"/>COPY DETAILS</button>";
    if (buttons.empty()) {
        h += "<div class=\"row\">" + ok + (copyText.empty() ? "" : copy) + "</div>";
    } else {
        h += "<div class=\"modal-col\">";
        int i = 0;
        for (auto& b : buttons)
            h += "<button class=\"nav\" data-action=\"" + b.action + "\" data-arg=\"" + b.arg + "\" data-key=\"mb" + std::to_string(i++) +
                 "\"><span class=\"marker\"/>" + esc(b.label) + "</button>";
        h += (copyText.empty() ? "" : copy) + ok + "</div>";
    }
    h += "</div></div>";
    if (auto* host = doc_->GetElementById("modal-host")) host->SetInnerRML(h);
    if (auto* ok = doc_->QuerySelector(".modal .default")) ok->Focus();
    refreshHints();
}

void App::closeModal() {
    modalOpen_ = false;
    if (auto* host = doc_->GetElementById("modal-host")) host->SetInnerRML("");
    render();
}

// ---------------------------------------------------------------- rendering

std::string App::focusKey() const {
    Rml::Element* f = doc_ ? doc_->GetContext()->GetFocusElement() : nullptr;
    while (f && !f->HasAttribute("data-key")) f = f->GetParentNode();
    return f ? f->GetAttribute<Rml::String>("data-key", "") : "";
}

void App::restoreFocus(const std::string& key) {
    Rml::Element* target = nullptr;
    if (!key.empty()) target = doc_->QuerySelector("[data-key=\"" + key + "\"]");
    if (!target || target->HasAttribute("disabled")) target = doc_->QuerySelector(".nav.default");
    if (!target) target = doc_->QuerySelector(".nav");
    // "Nearest": scrolls only if the element is out of view, and by the least amount.
    if (target) { target->Focus(); target->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest)); }
}

float App::pageScroll() const {
    Rml::Element* p = doc_ ? doc_->GetElementById("page") : nullptr;
    return p ? p->GetScrollTop() : 0.f;
}

std::string App::rowValue(const std::string& key) const {
    Rml::Element* e = doc_ ? doc_->QuerySelector("[data-key=\"" + key + "\"] .valtext") : nullptr;
    return e ? e->GetInnerRML() : "";
}

// Centre of a row control (part "value") or of its left / right step arrow, in window pixels.
bool App::elementCenter(const std::string& key, const std::string& part, float& x, float& y) const {
    Rml::Element* e = doc_ ? doc_->QuerySelector("[data-key=\"" + key + "\"]") : nullptr;
    if (!e) return false;
    if (part == "left" || part == "right") {
        Rml::ElementList arrows;
        e->GetElementsByTagName(arrows, "span");
        Rml::Element* hit = nullptr;
        for (auto* s : arrows)
            if (s->IsClassSet("stepbtn")) {
                if (part == "left" && !hit) hit = s;
                if (part == "right") hit = s;
            }
        if (!hit) return false;
        e = hit;
    }
    const Rml::Vector2f c = e->GetAbsoluteOffset(Rml::BoxArea::Border) + e->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
    x = c.x;
    y = c.y;
    return true;
}

void App::renderHeader() {
    if (auto* t = doc_->GetElementById("game-title")) t->SetInnerRML(esc(profile_.gameTitle));
    if (auto* m = doc_->GetElementById("game-meta")) {
        std::string meta = screen_ == Screen::Main ? "Xbox recomp · " + buildNumber() : "Xbox recomp";
        if (screen_ == Screen::Main && !state_.gameRegion.empty()) meta += " · your copy verified (" + state_.gameRegion + ")";
        m->SetInnerRML(esc(meta));
    }
    std::string right;
    if (screen_ == Screen::Main) {
        if (game_.running()) right = "<div class=\"status\">GAME RUNNING</div>";
        else
            right = "<button class=\"nav play default\" data-action=\"play\" data-key=\"play\"><span class=\"marker\"/>PLAY</button>";
    }
    if (auto* r = doc_->GetElementById("header-right")) r->SetInnerRML(right);
}

void App::render() {
    if (!screenEl_) return;
    // Keep the focused element across re-renders of the same screen; a new screen starts at its default.
    const std::string scope = screen_ == Screen::Main ? "main"
                              : "setup" + std::to_string(int(step_)) + (checking_ ? "c" : "");
    const std::string key = scope == lastScope_ ? focusKey() : std::string();
    // The option list keeps its scroll position when its values change (same screen, same tab).
    const float scroll = (scope == lastScope_ && tab_ == lastTab_) ? pageScroll() : 0.f;
    lastScope_ = scope;
    lastTab_ = tab_;
    renderHeader();
    screenEl_->SetInnerRML(screen_ == Screen::Setup ? renderSetup() : renderMain());
    if (scroll > 0.f) {
        doc_->GetContext()->Update();  // lay the new content out first, or the offset would be clamped to 0
        if (Rml::Element* p = doc_->GetElementById("page")) p->SetScrollTop(scroll);
    }
    if (!modalOpen_) restoreFocus(key);
    refreshHints();
    if (!pendingNotice_.empty() && screen_ == Screen::Main && !modalOpen_) {
        const std::string n = std::move(pendingNotice_);
        pendingNotice_.clear();
        showModal("Display settings changed", n, "");
    }
}

void App::refreshHints() {
    nav_.promptStyle = buttonPromptsValue();
    const HintLabels h = nav_.hints();
    std::vector<Hint> hints;
    if (modalOpen_ && browserOpen_) {
        hints = {{h.confirm, "open", false, h.confirmG}, {h.back, "up / cancel", false, h.backG}};
    } else if (modalOpen_) {
        hints = {{h.confirm, "ok", false, h.confirmG}};
    } else if (screen_ == Screen::Setup) {
        hints.push_back({h.confirm, "select", false, h.confirmG});
        if (step_ == SetupStep::Choose) {
            hints.push_back({h.x, "search drives", false, h.xG});
            if (canCancelSetup_) hints.push_back({h.back, "back", false, h.backG});
        } else if (step_ == SetupStep::Unpacking) {
            hints.clear();
            hints.push_back({h.confirm, "select", false, h.confirmG});
            hints.push_back({h.back, "cancel", false, h.backG});
        } else {
            hints.push_back({h.back, step_ == SetupStep::Searching && search_.running() ? "stop" : "back", false, h.backG});
        }
    } else if (!listenAction_.empty()) {
        hints = {{"Press the new button", "Esc or wait to cancel"}};
    } else {
        hints = {{h.confirm, "select", false, h.confirmG}, {h.back, "back", false, h.backG}, {h.tabs, "tabs", false, h.tabsG}, {h.start, "play", true, h.startG}};
    }
    std::string html;
    for (auto& x : hints)
    {
        std::string cap;
        if (x.pics.empty()) cap = "<span class=\"key" + std::string(x.yellow ? " start" : "") + "\">" + esc(x.key) + "</span>";
        else for (auto& g : x.pics) cap += views::glyphHtml(g);
        html += "<div class=\"hint\">" + cap + "<span>" + esc(x.text) + "</span></div>";
    }
    if (auto* g = doc_->GetElementById("hint-group")) g->SetInnerRML(html);
    if (auto* l = doc_->GetElementById("legal"))
        l->SetInnerRML("made with " + esc(profile_.brand) + " · Open source (GPL-2.0-or-later). No game files included. Bring your own copy.");
}

static std::string resultHeadline(IdStatus s);

std::string App::renderSetup() {
    std::string h = "<div class=\"center\">";
    const bool second = (step_ == SetupStep::Result && !checking_) || step_ == SetupStep::Unpack || step_ == SetupStep::Unpacking;
    const std::string step = std::string("STEP ") + (second ? "2" : "1") + " OF 2";
    h += "<div class=\"stepline\"><span class=\"chev left\"/><span>" + step + "</span><span class=\"chev right\"/></div>";

    if (step_ == SetupStep::Choose) {
        h += "<div class=\"title big-title\">Add your game</div>";
        h += "<p class=\"secondary lead\">Choose your disc image (.iso) or extracted game folder. The launcher works out which game it is and checks it. "
             "Your files are only read" + std::string(discImageOk() ? "; a disc image is played straight from the image.</p>" : "; a disc image is unpacked once into a folder next to the launcher.</p>");
        if (!setupNote_.empty()) h += "<div class=\"note\">" + esc(setupNote_) + "</div>";
        h += "<button class=\"nav bigbtn default\" data-action=\"choose-file\" data-key=\"choose-file\"><span class=\"marker\"/>"
             "<div class=\"title bigbtn-title\">CHOOSE DISC IMAGE (.ISO)...</div>"
             "<div class=\"secondary small\">or drag it onto this window · or choose your extracted game folder below</div></button>";
        h += "<div class=\"row center-row\">"
             "<button class=\"nav\" data-action=\"choose-folder\" data-key=\"choose-folder\"><span class=\"marker\"/>CHOOSE A FOLDER</button>"
             "<button class=\"nav\" data-action=\"search\" data-key=\"search\"><span class=\"marker\"/>SEARCH MY DRIVES</button></div>";
    } else if (step_ == SetupStep::Unpack) {
        h += "<div class=\"title big-title\">Unpack your disc image</div>";
        h += "<p class=\"secondary lead\">This disc image needs to be unpacked once (about " + esc(gbText(unpackBytes_)) + "). The game then runs from the unpacked folder.</p>";
        h += "<div class=\"table\"><div class=\"trow\"><span class=\"muted\">Unpack to</span><span class=\"tval\">" + esc(shortPath(pathToUtf8(unpackTarget_), 46)) + "</span></div></div>";
        if (!unpackNote_.empty()) h += "<div class=\"note\">" + esc(unpackNote_) + "</div>";
        h += "<div class=\"row center-row\">";
        if (unpackFits_)
            h += "<button class=\"nav play default\" data-action=\"unpack\" data-key=\"unpack\"><span class=\"marker\"/>UNPACK</button>";
        h += "<button class=\"nav" + std::string(unpackFits_ ? "" : " default") + "\" data-action=\"unpack-folder\" data-key=\"unpack-folder\"><span class=\"marker\"/>CHANGE FOLDER</button>"
             "<button class=\"nav\" data-action=\"restart\" data-key=\"restart\"><span class=\"marker\"/>BACK</button></div>";
        h += "<p class=\"muted small lead\">Your disc image is only read. The unpacked files are written to the folder above and nowhere else.</p>";
    } else if (step_ == SetupStep::Unpacking) {
        h += "<div class=\"title big-title\">Unpacking your game...</div>";
        h += "<p class=\"secondary lead\">This happens once. Please keep the launcher open.</p>";
        h += "<div class=\"progress\" id=\"unpack-bar\"></div>";
        h += "<div class=\"muted small\" id=\"unpack-text\">Starting...</div>";
        h += "<div class=\"row center-row\"><button class=\"nav default\" data-action=\"unpack-cancel\" data-key=\"unpack-cancel\"><span class=\"marker\"/>CANCEL</button></div>";
    } else if (step_ == SetupStep::Searching) {
        h += "<div class=\"title big-title\" id=\"search-title\">" + std::string(search_.running() ? "Looking for your game..." : (found_.empty() ? "Nothing found" : "Search finished")) + "</div>";
        h += "<p class=\"secondary lead\">Searching your drives for Xbox disc images and game folders. Files are only read.</p>";
        h += "<div class=\"muted small\" id=\"search-current\">" + esc(search_.running() ? shortPath(searchCurrent_) : searchCurrent_) + "</div>";
        h += "<div class=\"foundlist\">";
        for (size_t i = 0; i < found_.size(); ++i) {
            const auto& f = found_[i];
            const char* badge = f.id.status == IdStatus::Ok ? "VERIFIED" : f.id.status == IdStatus::WrongVersion ? "OTHER VERSION" : "OTHER GAME";
            const std::string name = f.id.title.empty() ? pathToUtf8(f.path.filename()) : f.id.title;
            h += "<button class=\"nav found" + std::string(i == 0 ? " default" : "") + "\" data-action=\"pick-found\" data-arg=\"" +
                 std::to_string(i) + "\" data-key=\"found-" + std::to_string(i) + "\"><span class=\"marker\"/>"
                 "<div class=\"foundrow\"><div class=\"fcol\"><div class=\"fname\">" + esc(name) + "</div><div class=\"muted small\">" +
                 esc(f.id.kind + " · " + shortPath(pathToUtf8(f.path), 44)) + "</div></div><span class=\"badge" +
                 (f.id.status == IdStatus::Ok ? "" : " warn") + "\">" + badge + "</span></div></button>";
        }
        h += "</div>";
        h += "<div class=\"row center-row\"><button class=\"nav" + std::string(found_.empty() ? " default" : "") +
             "\" data-action=\"restart\" data-key=\"restart\"><span class=\"marker\"/>" + (search_.running() ? "STOP" : "BACK") + "</button></div>";
    } else {  // Result
        if (checking_) {
            h += "<div class=\"title big-title\">Checking your game...</div>";
            h += "<p class=\"secondary lead\">Reading " + esc(shortPath(pathToUtf8(checkPath_))) + ". This takes a moment.</p>";
            h += "<div class=\"row center-row\"><button class=\"nav default\" data-action=\"restart\" data-key=\"restart\"><span class=\"marker\"/>CANCEL</button></div>";
        } else {
            const bool ok = result_.status == IdStatus::Ok;
            h += "<div class=\"title big-title" + std::string(ok ? "" : " warnhead") + "\">" + resultHeadline(result_.status) + "</div>";
            if (ok || !result_.title.empty()) {
                h += "<div class=\"table\">";
                auto row = [&](const std::string& k, const std::string& v, bool good) {
                    h += "<div class=\"trow\"><span class=\"muted\">" + esc(k) + "</span><span class=\"tval" + (good ? " good" : "") + "\">" + esc(v) + "</span></div>";
                };
                row("Game", result_.title.empty() ? profile_.gameTitle : result_.title, false);
                row("Version", "Xbox · " + (result_.region.empty() ? std::string("unknown region") : result_.region), false);
                row("Check", ok ? "Matches a known good copy" : "Does not match", ok);
                row(result_.kind == "folder" ? "Folder" : "File", shortPath(pathToUtf8(resultPath_)), false);
                h += "</div>";
            }
            if (!ok) h += "<div class=\"note\">" + esc(result_.message) + "</div>";
            if (ok) {
                h += "<div class=\"row center-row\"><button class=\"nav play default\" data-action=\"accept\" data-key=\"accept\"><span class=\"marker\"/>USE THIS GAME</button>"
                     "<button class=\"nav\" data-action=\"restart\" data-key=\"restart\"><span class=\"marker\"/>PICK A DIFFERENT FILE</button></div>";
            } else {
                h += "<div class=\"row center-row\"><button class=\"nav default\" data-action=\"restart\" data-key=\"restart\"><span class=\"marker\"/>PICK A DIFFERENT FILE</button>"
                     "<button class=\"nav\" data-action=\"search\" data-key=\"search\"><span class=\"marker\"/>SEARCH MY DRIVES</button></div>";
            }
            h += "<p class=\"muted small lead\">Your file is only read. Nothing is copied or changed.</p>";
        }
    }
    h += "</div>";
    return h;
}

// Headline of the "game check" result, in plain words for each outcome.
static std::string resultHeadline(IdStatus s) {
    switch (s) {
        case IdStatus::Ok: return "Found it";
        case IdStatus::WrongVersion: return "This version is not supported";
        case IdStatus::WrongGame: return "This is a different game";
        case IdStatus::OtherPlatform: return "This is not the Xbox version";
        case IdStatus::NotAGame:
        case IdStatus::NoXbe: return "This is not a game";
        case IdStatus::NotFound: return "Your game was not found";
        case IdStatus::CantRead: return "The game could not be read";
    }
    return "That is not quite right";
}

std::string App::renderMain() {
    static const struct { const char* id; const char* label; Tab t; } tabs[] = {
        {"overview", "OVERVIEW", Tab::Overview}, {"video", "VIDEO", Tab::Video}, {"enhance", "ENHANCEMENTS", Tab::Enhance},
        {"mods", "MODS", Tab::Mods}, {"controllers", "CONTROLLERS", Tab::Controllers}};
    std::string h = "<div class=\"row tabs indent\">";
    for (auto& t : tabs)
        h += std::string("<button class=\"nav tab") + (tab_ == t.t ? " selected" : "") + "\" data-action=\"tab\" data-arg=\"" + t.id +
             "\" data-key=\"tab-" + t.id + "\"><span class=\"marker\"/>" + t.label + "</button>";
    h += "</div><div id=\"page\">";
    switch (tab_) {
        case Tab::Overview: h += renderOverview(); break;
        case Tab::Video: h += renderRows("video"); break;
        case Tab::Enhance: h += "<p class=\"secondary\">All off by default. Off is the original game.</p>" + renderRows("enhance"); break;
        case Tab::Controllers: h += renderControllers(); break;
        case Tab::Mods: h += renderMods(); break;
    }
    h += "</div>";
    return h;
}

std::string App::renderOverview() {
    const std::string active = settings_.activePreset();
    std::string h = "<div class=\"gap14\"><div class=\"section-label\">PRESET</div><div class=\"row indent\">";
    for (auto& p : settings_.presets())
        h += std::string("<button class=\"nav preset") + (active == p.id ? " selected" : "") + "\" data-action=\"preset\" data-arg=\"" + p.id +
             "\" data-key=\"preset-" + p.id + "\"><span class=\"marker\"/>" + esc(p.name) + "</button>";
    h += "</div>";
    const SettingPreset* cur = settings_.preset(active);
    if (state_.firstStartNote)
        h += "<div class=\"note\" style=\"margin-top: 10dp\">Welcome. Your settings start on the <b>MAX</b> preset (widescreen, full resolution, everything unlocked). "
             "Pick <b>ORIGINAL</b> for the game exactly as it was, or change single settings on the other tabs. Then press PLAY. "
             "Your game files are never changed.</div>";
    h += "<p id=\"preset-desc\" class=\"secondary\">" +
         esc(cur ? cur->description : "Custom. You changed individual settings. Pick a preset to start over.") + "</p>";

    auto cell = [&](const std::string& k, const std::string& v, bool good) {
        return "<div class=\"cell\"><div class=\"muted cellk\">" + esc(k) + "</div><div class=\"cellv" + (good ? " good" : "") + "\">" + esc(v) + "</div></div>";
    };
    h += "<div class=\"grid3\">";
    h += cell("GAME", state_.gameRegion.empty() ? "Verified" : "Verified · " + state_.gameRegion, true);
    h += cell("BUILD", buildNumber(), false);
    h += cell("SETTINGS", cur ? "Preset " + cur->name : "Custom", false);
    h += "</div>";

    h += "<div class=\"section-label\">YOUR GAME FILE</div><div class=\"row filerow\"><span class=\"secondary\">" +
         esc(shortPath(state_.gamePath)) + "</span>"
         "<button class=\"nav\" data-action=\"change-game\" data-key=\"change-game\"><span class=\"marker\"/>CHANGE GAME</button></div>";
    h += "<div class=\"section-label\">LAUNCHER</div>";
    if (!state_.latestTag.empty() && isNewerVersion(state_.latestTag, LAUNCHER_VERSION)) {
        h += "<div class=\"row filerow\"><span class=\"good\">A new version is available: " + esc(state_.latestTag) + " (you have " + LAUNCHER_VERSION +
             ")</span><button class=\"nav\" data-action=\"open-update\" data-key=\"open-update\"><span class=\"marker\"/>OPEN DOWNLOAD PAGE</button></div>";
    }
    h += rowHtml("update-check", "Check for updates", "Once a day the launcher asks GitHub for the newest release number. Nothing is downloaded or installed.",
                 state_.updateCheck ? "ON" : "OFF", state_.updateCheck, false);
    h += rowHtml("ui-size", "UI size", "Larger or smaller text and buttons. It already follows your Windows display scaling.",
                 std::to_string(state_.uiSize) + "%", false, true);
    h += "</div>";
    return h;
}

std::string App::renderRows(const std::string& page) { return views::settingsRows(model_, page); }

// ---------------------------------------------------------------- update check

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

void App::startUpdateCheck() {
    // LAUNCHER_UPDATE_URL replaces GitHub for tests ("off" = never check).
    const char* over = std::getenv("LAUNCHER_UPDATE_URL");
    if (over && std::string(over) == "off") return;
    if (!state_.updateCheck || updateBusy_) return;
    const int64_t now = int64_t(std::time(nullptr));
    if (!over && state_.lastUpdateCheck != 0 && now - state_.lastUpdateCheck < 24 * 3600 && now >= state_.lastUpdateCheck) return;
    const std::string url = over ? over : "https://api.github.com/repos/" + profile_.updateRepo + "/releases?per_page=10";
    const std::string agent = std::string("recomp-launcher/") + LAUNCHER_VERSION;
    updateBusy_ = true;
    updateFetch_ = std::async(std::launch::async, [url, agent] {
        std::string body;
        return httpGet(url, agent, 5000, body) ? body : std::string();  // empty = quiet failure
    });
}

void App::pollUpdateCheck() {
    if (!updateBusy_ || !updateFetch_.valid() || updateFetch_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
    updateBusy_ = false;
    const std::string body = updateFetch_.get();
    state_.lastUpdateCheck = int64_t(std::time(nullptr));  // also after a failure: do not retry on every start while offline
    UpdateInfo u;
    if (!body.empty() && parseLatestRelease(body, u)) {
        state_.latestTag = u.tag;
        state_.latestUrl = u.url;
    }
    std::string err;
    state_.save(model_.statePath(), err);  // a failed save only means we ask again next time
    if (screen_ == Screen::Main && tab_ == Tab::Overview && !modalOpen_) render();
}
