// The Mods tab: install a mod (zip or folder), turn mods on and off, change their load order, remove them.
// The mod logic is in core/mods.*; the game reads <data>\mods through STE_MODS_DIR (order.json holds order + enabled flags).
#include <SDL3/SDL.h>

#include <algorithm>

#include "core/identify.h"
#include "platform/ModTool.h"
#include "ui/App.h"

namespace fs = std::filesystem;

void SDLCALL App::modDialogDone(void* ud, const char* const* list, int) {
    auto* self = static_cast<App*>(ud);
    if (!list || !list[0]) return;
    std::lock_guard<std::mutex> l(self->dlgMu_);
    self->dlgMod_ = list[0];
}

std::string App::renderMods() {
    views::ModsViewState v;
    v.busy = modBusy_; v.patchBusy = patchBusy_; v.busyName = modBusyName_; v.note = modsNote_; v.patchReports = &patchReports_;
    return views::modsPage(model_, v);
}

bool App::modsAct(const std::string& action, const std::string& arg) {
    if (action.rfind("mod", 0) != 0) return false;
    std::string err;
    auto busyNote = [&] {
        showModal("Please wait", "A mod is still being installed.", "");
    };
    if (action == "mods-install-zip") {
        if (modBusy_) return busyNote(), true;
        openBrowser(BrowseFor::ModZip);
    } else if (action == "mods-install-folder") {
        if (modBusy_) return busyNote(), true;
        openBrowser(BrowseFor::ModFolder);
    } else if (action == "mods-open") {
        std::error_code ec;
        fs::create_directories(modsRoot(), ec);
        std::string url = pathToUtf8(fs::absolute(modsRoot()));
        std::replace(url.begin(), url.end(), '\\', '/');
        SDL_OpenURL(("file:///" + url).c_str());
    } else if (action == "mods-prepare") {
        if (modBusy_ || patchBusy_) return busyNote(), true;
        patchFailed_.clear();
        startPatchPrepare(false, false, false);
    } else if (action == "modal-play-anyway") {
        closeModal();
        skipPrepare_ = true;
        play(patchOrig_, patchBorderless_);
    } else if (action == "mod-toggle") {
        if (const ModInfo* m = mods_.find(arg)) mods_.setEnabled(arg, !m->enabled);
        if (!mods_.save(err)) showModal("Could not save the mod list", err, "");
        modsNote_.clear();
        render();
    } else if (action == "mod-up" || action == "mod-down") {
        mods_.move(arg, action == "mod-up" ? -1 : +1);
        if (!mods_.save(err)) showModal("Could not save the mod list", err, "");
        modsNote_.clear();
        render();
    } else if (action == "mod-remove") {
        const ModInfo* m = mods_.find(arg);
        if (!m) return true;
        showModalEx("Remove this mod?", "\"" + m->title() + "\" will be taken out of the game. It is moved to the _removed folder inside your mods folder, so you can get it back.",
                    "", "", {{"REMOVE", "modal-mod-remove-yes", arg}});
    } else if (action == "modal-mod-remove-yes") {
        closeModal();
        if (game_.running()) { showModal("Close the game first", "A mod cannot be removed while the game is running.", ""); return true; }
        const ModInfo* m = mods_.find(arg);
        const std::string name = m ? m->title() : arg;
        if (mods_.remove(arg, err)) modsNote_ = "Removed " + name + ".";
        else showModal("Could not remove the mod", err, "");
        render();
    } else if (action == "modal-mod-replace") {
        closeModal();
        if (game_.running()) { showModal("Close the game first", "A mod cannot be replaced while the game is running.", ""); return true; }
        startModInstall(modPending_, true);
    } else {
        return false;
    }
    return true;
}

void App::startModInstall(const fs::path& src, bool replace) {
    if (modBusy_) return;
    mods_.setRoot(modsRoot());
    std::error_code ec;
    const bool isZip = !fs::is_directory(src, ec);
    modBusy_ = true;
    modBusyName_ = pathToUtf8(src.filename());
    modsNote_.clear();
    ModStore copy = mods_;  // the worker uses its own copy; the UI reloads when it is done
    modInstall_ = std::async(std::launch::async, [copy, src, replace, isZip]() mutable {
        return isZip ? copy.installZip(src, replace) : copy.installFolder(src, replace);
    });
    render();
}

void App::pollModInstall() {
    if (!modBusy_ || modInstall_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const ModInstallResult r = modInstall_.get();
    modBusy_ = false;
    reloadMods();
    if (r.kind == ModInstallResult::Ok) {
        std::string err;
        mods_.save(err);  // write the new mod into order.json so the load order is explicit
        modsNote_ = "Installed " + r.name + ". It is turned on; press PLAY to use it.";
    } else if (r.kind == ModInstallResult::AlreadyInstalled) {
        modPending_ = r.sourceRoot;
        showModalEx("This mod is already installed", "\"" + r.name + "\" is already in your mods folder. Replace it with this copy? The old one is moved to _removed.",
                    "", "", {{"REPLACE IT", "modal-mod-replace", ""}});
    } else {
        showModal("Could not install the mod", r.error.empty() ? "Something went wrong." : r.error, "");
    }
    render();
}

// ---------------------------------------------------------------- patch mods

static bool modFits(const ModStore& store, const ModInfo& m, const std::string& sha, const std::string& game) {
    return !store.status(m, sha, game).problem();
}

void App::refreshPatchStates() {
    patchReports_.clear();
    if (playingFromImage()) {  // plain patch mods are built from game files in a folder; recipe mods can be built from the image itself
        for (auto& m : mods_.mods())
            if (modNeedsExternalTool(m)) patchReports_[m.id] = quickPatchStates(m, {}, &mods_);
        for (auto& m : mods_.mods())
            if (!m.patches.empty() && !modNeedsExternalTool(m) && m.enabled) { modsNote_ = "Mods that patch the game's files need an unpacked game folder, so they are not applied while you play from a disc image. Unpack the image into a folder and choose that folder to use them."; break; }
        return;
    }
    const fs::path gameDir = state_.gamePath.empty() ? fs::path() : pathFromUtf8(state_.gamePath);
    for (auto& m : mods_.mods())
        if (!m.patches.empty()) patchReports_[m.id] = quickPatchStates(m, gameDir, &mods_);
}

// True when an enabled mod that fits the game has a patch that is not built (or not current).
bool App::patchesNeedPrepare() const {
    if (state_.gamePath.empty()) return false;
    const fs::path gameDir = pathFromUtf8(state_.gamePath);
    for (auto& m : mods_.mods()) {
        if (m.patches.empty() || (playingFromImage() && !modNeedsExternalTool(m)) || !m.enabled || !m.problem.empty() || patchFailed_.count(m.id) || !modFits(mods_, m, state_.sha256, profile_.expectedGameId)) continue;
        for (auto& r : quickPatchStates(m, gameDir, &mods_))
            if (!r.ok()) return true;
    }
    return false;
}

void App::startPatchPrepare(bool thenPlay, bool original, bool borderless) {
    if (patchBusy_ || state_.gamePath.empty()) return;
    const bool image = playingFromImage();
    patchBusy_ = true;
    patchThenPlay_ = thenPlay;
    patchOrig_ = original;
    patchBorderless_ = borderless;
    patchCancel_ = false;
    modsNote_.clear();
    if (thenPlay) showModal("Preparing your mods", "Building the patched files from your game files. The game starts when this is done.", "");
    ModStore copy = mods_;
    const fs::path gameDir = pathFromUtf8(state_.gamePath);
    const std::string sha = state_.sha256, gameId = profile_.expectedGameId;
    const fs::path exeDir = paths_.exeDir;
    patchRun_ = std::async(std::launch::async, [copy, gameDir, sha, gameId, exeDir, image, this]() {
        return preparePatches(copy, gameDir, [&](const ModInfo& m) { return modFits(copy, m, sha, gameId) && (!image || modNeedsExternalTool(m)); }, &patchCancel_,
                              [&](const fs::path& modDir, const fs::path& game) { return runModTool(exeDir, modDir, game, &patchCancel_); },
                              [&](const fs::path& root, const fs::path& game, const std::vector<std::string>& ids) { return runModToolStack(exeDir, root, game, ids, &patchCancel_); });
    });
    render();
}

void App::pollPatchPrepare() {
    if (!patchBusy_ || patchRun_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const PatchRunResult r = patchRun_.get();
    patchBusy_ = false;
    if (modalOpen_ && patchThenPlay_) closeModal();
    refreshPatchStates();
    for (auto& rep : r.reports)  // what this run found replaces the quick states
        if (rep.state != PatchState::Skipped) {
            auto& v = patchReports_[rep.modId];
            for (auto& q : v)
                if (q.target == rep.target) q = rep;
        }
    std::string bad;
    for (auto& rep : r.reports)
        if (!rep.ok() && rep.state != PatchState::Skipped) {
            patchFailed_.insert(rep.modId);
            bad += "\n- " + rep.modId + ", " + rep.target + ": " + rep.detail;
        }
    if (r.failed > 0) {
        const std::string body = "Some patched files could not be built:" + bad + "\nThe game skips those mods.";
        if (patchThenPlay_) showModalEx("Some mods could not be prepared", body, "", "", {{"PLAY ANYWAY", "modal-play-anyway", ""}});
        else showModal("Some mods could not be prepared", body, "");
    } else {
        modsNote_ = r.built ? "Prepared " + std::to_string(r.built) + " patched file" + (r.built == 1 ? "" : "s") + "." : "Everything is already prepared.";
        if (patchThenPlay_) { skipPrepare_ = true; play(patchOrig_, patchBorderless_); }
    }
    render();
}