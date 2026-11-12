// The in-launcher file browser: shown as a message-box style panel so focus, Back and pad navigation behave like every other box.
// "Choose disc image", "Choose a folder", "Unpack to" and "Install a mod" use it; the Windows dialog is one button away.
#include <SDL3/SDL.h>

#include <algorithm>

#include "core/identify.h"
#include "ui/App.h"
#include "ui/views/browser.h"

namespace fs = std::filesystem;

void App::openBrowser(BrowseFor what) {
    browseFor_ = what;
    std::string title;
    std::vector<std::string> exts;
    BrowseKind kind = BrowseKind::Folder;
    switch (what) {
        case BrowseFor::Game: title = "Choose your disc image (.iso)"; kind = BrowseKind::File; exts = {".iso", ".xiso"}; break;
        case BrowseFor::GameFolder: title = "Choose your game folder (the one that holds default.xbe)"; break;
        case BrowseFor::UnpackParent: title = "Choose where to unpack the game (a folder named STE is made inside)"; break;
        case BrowseFor::ModZip: title = "Choose a mod (.zip)"; kind = BrowseKind::File; exts = {".zip"}; break;
        case BrowseFor::ModFolder: title = "Choose a mod folder"; break;
    }
    std::vector<fs::path> recents;
    for (auto& r : state_.recentDirs) recents.push_back(pathFromUtf8(r));
    fs::path start;
    if ((what == BrowseFor::Game || what == BrowseFor::GameFolder) && !state_.gamePath.empty()) start = pathFromUtf8(state_.gamePath).parent_path();
    browser_.open(kind, title, exts, start, std::move(recents));
    browserOpen_ = true;
    modalOpen_ = true;
    renderBrowser();
}

void App::renderBrowser(const std::string& focusKey) {
    if (auto* host = doc_->GetElementById("modal-host")) host->SetInnerRML(views::browserPanel(browser_, "WINDOWS DIALOG"));
    doc_->GetContext()->Update();
    Rml::Element* e = focusKey.empty() ? nullptr : doc_->QuerySelector("[data-key=\"" + focusKey + "\"]");
    if (!e) e = doc_->QuerySelector(".modal .default");
    if (!e) e = doc_->QuerySelector(".modal .br-entry");
    if (!e) e = doc_->QuerySelector(".modal .nav");
    if (e) e->Focus();
    refreshHints();
}

void App::closeBrowser() {
    browserOpen_ = false;
    closeModal();
}

void App::browserBack() {
    if (browser_.up()) renderBrowser();
    else closeBrowser();
}

// Remembers the folder (newest first, at most 8) and hands the choice over exactly like the Windows dialogs do.
void App::browserPicked(const fs::path& p) {
    std::error_code ec;
    const fs::path folder = fs::is_directory(p, ec) ? p : p.parent_path();
    const std::string f = pathToUtf8(folder);
    state_.recentDirs.erase(std::remove(state_.recentDirs.begin(), state_.recentDirs.end(), f), state_.recentDirs.end());
    state_.recentDirs.insert(state_.recentDirs.begin(), f);
    if (state_.recentDirs.size() > 8) state_.recentDirs.resize(8);
    std::string err;
    state_.save(model_.statePath(), err);
    closeBrowser();
    const std::string u = pathToUtf8(p);
    std::lock_guard<std::mutex> l(dlgMu_);
    switch (browseFor_) {
        case BrowseFor::Game:
        case BrowseFor::GameFolder: dlgPath_ = u; break;
        case BrowseFor::UnpackParent: dlgParent_ = u; break;
        case BrowseFor::ModZip:
        case BrowseFor::ModFolder: dlgMod_ = u; break;
    }
}

void App::openNativeDialog(BrowseFor what) {
    switch (what) {
        case BrowseFor::Game: {
            static const SDL_DialogFileFilter filters[] = {{"Xbox disc images", "iso;xiso"}, {"All files", "*"}};
            SDL_ShowOpenFileDialog(&App::fileDialogDone, this, window_, filters, 2, nullptr, false);
            break;
        }
        case BrowseFor::GameFolder: SDL_ShowOpenFolderDialog(&App::fileDialogDone, this, window_, nullptr, false); break;
        case BrowseFor::UnpackParent: SDL_ShowOpenFolderDialog(&App::unpackDirDone, this, window_, nullptr, false); break;
        case BrowseFor::ModZip: {
            static const SDL_DialogFileFilter filters[] = {{"Mod archives", "zip"}, {"All files", "*"}};
            SDL_ShowOpenFileDialog(&App::modDialogDone, this, window_, filters, 2, nullptr, false);
            break;
        }
        case BrowseFor::ModFolder: SDL_ShowOpenFolderDialog(&App::modDialogDone, this, window_, nullptr, false); break;
    }
}

bool App::browserAct(const std::string& action, const std::string& arg) {
    if (!browserOpen_ || action.rfind("modal-br-", 0) != 0) return false;
    const size_t n = size_t(std::max(0, std::atoi(arg.c_str())));
    if (action == "modal-br-open") {
        fs::path picked;
        if (browser_.enter(n, picked)) browserPicked(picked);
        else renderBrowser();
    } else if (action == "modal-br-up") {
        browser_.up();
        renderBrowser(browser_.atDriveList() ? "br-cancel" : "");
    } else if (action == "modal-br-place") {
        if (n < browser_.places().size()) browser_.goTo(browser_.places()[n].path);
        renderBrowser();
    } else if (action == "modal-br-recent") {
        if (n < browser_.recents().size()) browser_.goTo(browser_.recents()[n]);
        renderBrowser();
    } else if (action == "modal-br-pc") {
        browser_.goDriveList();
        renderBrowser();
    } else if (action == "modal-br-use") {
        if (!browser_.atDriveList()) browserPicked(browser_.dir());
    } else if (action == "modal-br-native") {
        const BrowseFor what = browseFor_;
        closeBrowser();
        openNativeDialog(what);
    } else if (action == "modal-br-cancel") {
        closeBrowser();
    }
    return true;
}
