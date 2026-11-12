// The Mods tab: install a mod (zip or folder), turn mods on and off, change their load order, remove them.
// The mod logic is in core/mods.*; the game reads <data>\mods through STE_MODS_DIR (order.json holds order + enabled flags).
#include <SDL3/SDL.h>

#include <algorithm>

#include "core/identify.h"
#include "ui/App.h"

namespace fs = std::filesystem;

void SDLCALL App::modDialogDone(void* ud, const char* const* list, int) {
    auto* self = static_cast<App*>(ud);
    if (!list || !list[0]) return;
    std::lock_guard<std::mutex> l(self->dlgMu_);
    self->dlgMod_ = list[0];
}

std::string App::renderMods() {
    std::string h;
    if (profile_.modsDirEnv.empty()) return "<p class=\"secondary\">This game does not support mods.</p>";

    h += "<p class=\"secondary\">Mods change what the game loads. Your game files are never modified. "
         "Changes apply the next time you press PLAY. When two mods change the same file, the one lower in the list wins.</p>";
    h += "<div class=\"row indent\" style=\"margin-top: 14dp\">"
         "<button class=\"nav\" data-action=\"mods-install-zip\" data-key=\"mods-zip\"><span class=\"marker\"/>INSTALL FROM ZIP...</button>"
         "<button class=\"nav\" data-action=\"mods-install-folder\" data-key=\"mods-folder\"><span class=\"marker\"/>INSTALL FROM FOLDER...</button>"
         "<button class=\"nav\" data-action=\"mods-open\" data-key=\"mods-open\"><span class=\"marker\"/>OPEN MODS FOLDER</button></div>";
    h += "<div class=\"muted small\" style=\"display: block; padding-left: 26dp; margin-top: 6dp\">You can also drag a mod's .zip or folder onto this window.</div>";
    if (modBusy_) h += "<div class=\"good\" style=\"display: block; padding-left: 26dp; margin-top: 10dp\">Installing " + esc(modBusyName_) + "...</div>";
    else if (!modsNote_.empty()) h += "<div class=\"good\" style=\"display: block; padding-left: 26dp; margin-top: 10dp\">" + esc(modsNote_) + "</div>";

    const auto& list = mods_.mods();
    if (list.empty()) return h + "<p class=\"secondary\" style=\"margin-top: 18dp; padding-left: 26dp\">No mods installed yet.</p>";

    h += "<div class=\"section-label grouphead\" style=\"display: block\">INSTALLED · load order, first to last</div>";
    for (size_t i = 0; i < list.size(); ++i) {
        const ModInfo& m = list[i];
        const ModStatus st = mods_.status(m, state_.sha256, profile_.expectedGameId);
        std::string title = esc(m.title());
        if (!m.version.empty()) title += " <span class=\"muted small\">v" + esc(m.version) + "</span>";
        std::string by;
        for (auto& a : m.authors) by += (by.empty() ? "" : ", ") + a;
        std::string line2 = m.problem.empty() ? "#" + std::to_string(i + 1) : "";
        if (!by.empty()) line2 += (line2.empty() ? "" : " · ") + std::string("by ") + by;
        if (!m.profile.empty()) line2 += (line2.empty() ? "" : " · ") + m.profile;

        h += "<div class=\"setrow\"><div class=\"setlabel\"><div class=\"lbl\">" + title + "</div>";
        if (!m.description.empty()) h += "<div class=\"muted small\">" + esc(m.description) + "</div>";
        if (!line2.empty()) h += "<div class=\"muted small\">" + esc(line2) + "</div>";
        h += std::string("<div class=\"small ") + (st.problem() ? "warnline" : "good") + "\">" + esc(st.text) + "</div>";
        h += "<div class=\"row\" style=\"margin-top: 8dp; gap: 22dp\">";
        if (m.problem.empty()) {
            if (i > 0)
                h += "<button class=\"nav\" data-action=\"mod-up\" data-arg=\"" + esc(m.id) + "\" data-key=\"mod-up-" + esc(m.id) +
                     "\"><span class=\"marker\"/>EARLIER</button>";
            if (i + 1 < list.size() && list[i + 1].problem.empty())
                h += "<button class=\"nav\" data-action=\"mod-down\" data-arg=\"" + esc(m.id) + "\" data-key=\"mod-down-" + esc(m.id) +
                     "\"><span class=\"marker\"/>LATER</button>";
        }
        h += "<button class=\"nav\" data-action=\"mod-remove\" data-arg=\"" + esc(m.id) + "\" data-key=\"mod-rm-" + esc(m.id) +
             "\"><span class=\"marker\"/>REMOVE</button></div></div>";
        if (m.problem.empty())
            h += std::string("<button class=\"nav val") + (m.enabled ? " on" : "") + "\" data-action=\"mod-toggle\" data-arg=\"" + esc(m.id) +
                 "\" data-key=\"mod-toggle-" + esc(m.id) + "\"><span class=\"marker\"/><span class=\"valtext\">" + (m.enabled ? "ON" : "OFF") +
                 "</span></button>";
        h += "</div>";
    }    return h;
}

bool App::modsAct(const std::string& action, const std::string& arg) {
    if (action.rfind("mod", 0) != 0) return false;
    std::string err;
    auto busyNote = [&] {
        showModal("Please wait", "A mod is still being installed.", "");
    };
    if (action == "mods-install-zip") {
        if (modBusy_) return busyNote(), true;
        static const SDL_DialogFileFilter filters[] = {{"Mod archives", "zip"}, {"All files", "*"}};
        SDL_ShowOpenFileDialog(&App::modDialogDone, this, window_, filters, 2, nullptr, false);
    } else if (action == "mods-install-folder") {
        if (modBusy_) return busyNote(), true;
        SDL_ShowOpenFolderDialog(&App::modDialogDone, this, window_, nullptr, false);
    } else if (action == "mods-open") {
        std::error_code ec;
        fs::create_directories(modsRoot(), ec);
        std::string url = pathToUtf8(fs::absolute(modsRoot()));
        std::replace(url.begin(), url.end(), '\\', '/');
        SDL_OpenURL(("file:///" + url).c_str());
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
    mods_.reload();
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
