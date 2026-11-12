#include "ui/views/browser.h"

#include "core/identify.h"
#include "ui/views/rows.h"

namespace views {

namespace {

std::string btn(const std::string& label, const std::string& action, const std::string& arg, const std::string& key, bool def = false,
                const char* extra = "") {
    return std::string("<button class=\"nav ") + extra + (def ? " default" : "") + "\" data-action=\"" + action + "\" data-arg=\"" + esc(arg) +
           "\" data-key=\"" + key + "\"><span class=\"marker\"/>" + esc(label) + "</button>";
}

}  // namespace

std::string browserPanel(const FileBrowser& b, const std::string& nativeLabel) {
    std::string h = "<div class=\"modal-back\"><div class=\"modal modal-panel browser-panel\"><div class=\"title modal-title\">" + esc(b.title()) +
                    "</div><p class=\"mono muted br-path\">" + esc(b.pathText()) + "</p>";
    const bool folderMode = b.kind() == BrowseKind::Folder;

    // actions
    h += "<div class=\"row br-actions\">";
    if (folderMode && !b.atDriveList()) h += btn("USE THIS FOLDER", "modal-br-use", "", "br-use", true);
    h += btn("UP", "modal-br-up", "", "br-up", false, b.atDriveList() ? "disabled-look" : "");
    h += btn("CANCEL", "modal-br-cancel", "", "br-cancel");
    if (!nativeLabel.empty()) h += btn(nativeLabel, "modal-br-native", "", "br-native");
    h += "</div>";

    // places and recent folders
    h += "<div class=\"row br-places\">";
    h += btn("THIS PC", "modal-br-pc", "", "br-pc");
    int i = 0;
    for (auto& p : b.places()) {
        h += btn(p.label, "modal-br-place", std::to_string(i), "br-place-" + std::to_string(i));
        ++i;
    }
    h += "</div>";
    if (!b.recents().empty()) {
        h += "<div class=\"section-label br-label\">RECENT</div><div class=\"row br-places\">";
        i = 0;
        for (auto& r : b.recents()) {
            const std::string name = pathToUtf8(r.filename().empty() ? r : r.filename());
            h += btn(name, "modal-br-recent", std::to_string(i), "br-recent-" + std::to_string(i));
            ++i;
        }
        h += "</div>";
    }

    // list
    h += "<div class=\"section-label br-label\">" + std::string(b.atDriveList() ? "DRIVES" : folderMode ? "FOLDERS" : "FOLDERS AND FILES") + "</div>";
    h += "<div class=\"br-list\">";
    if (b.entries().empty()) {
        h += "<p class=\"muted small\" style=\"padding-left: 12dp\">" +
             esc(folderMode ? "No folders in here. Use UP, or USE THIS FOLDER." : "Nothing here that matches. Use UP, or pick another place.") + "</p>";
    }
    size_t idx = 0;
    for (auto& e : b.entries()) {
        std::string label = e.dir ? (b.atDriveList() ? e.name : e.name + "\\") : e.name;
        std::string right = e.dir ? "" : FileBrowser::sizeText(e.size);
        h += "<button class=\"nav br-entry" + std::string(e.dir ? " br-dir" : " br-file") + "\" data-action=\"modal-br-open\" data-arg=\"" + std::to_string(idx) +
             "\" data-key=\"br-e-" + std::to_string(idx) + "\"><span class=\"marker\"/><span class=\"br-name\">" + esc(label) + "</span><span class=\"br-size\">" + esc(right) +
             "</span></button>";
        ++idx;
    }
    if (b.truncated()) h += "<p class=\"muted small\" style=\"padding-left: 12dp\">Only the first entries are shown. Pick a subfolder, or use the " + esc(nativeLabel.empty() ? "search" : "Windows dialog") + ".</p>";
    h += "</div></div></div>";
    return h;
}

}  // namespace views
