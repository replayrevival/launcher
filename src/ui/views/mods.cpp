#include "ui/views/mods.h"

#include "ui/views/rows.h"

namespace views {

std::string modsPage(const LauncherModel& m_, const ModsViewState& v) {
    std::string h;
    if (m_.profile.modsDirEnv.empty()) return "<p class=\"secondary\">This game does not support mods.</p>";

    h += "<p class=\"secondary\">Mods change what the game loads. Your game files are never modified. "
         "Changes apply the next time you press PLAY. When two mods change the same file, the one lower in the list wins.</p>";
    h += "<div class=\"row indent\" style=\"margin-top: 14dp\">"
         "<button class=\"nav\" data-action=\"mods-install-zip\" data-key=\"mods-zip\"><span class=\"marker\"/>INSTALL FROM ZIP...</button>"
         "<button class=\"nav\" data-action=\"mods-install-folder\" data-key=\"mods-folder\"><span class=\"marker\"/>INSTALL FROM FOLDER...</button>"
         "<button class=\"nav\" data-action=\"mods-open\" data-key=\"mods-open\"><span class=\"marker\"/>OPEN MODS FOLDER</button></div>";
    bool anyPatch = false;
    for (auto& m : m_.mods.mods()) anyPatch = anyPatch || !m.patches.empty();
    if (anyPatch) {
        h.insert(h.rfind("</div>"), "<button class=\"nav\" data-action=\"mods-prepare\" data-key=\"mods-prepare\"><span class=\"marker\"/>PREPARE PATCHES</button>");
    }
    h += "<div class=\"muted small\" style=\"display: block; padding-left: 26dp; margin-top: 6dp\">You can also drag a mod's .zip or folder onto this window.</div>";
    if (v.patchBusy) h += "<div class=\"good\" style=\"display: block; padding-left: 26dp; margin-top: 10dp\">Preparing the patched files of your mods...</div>";
    if (v.busy) h += "<div class=\"good\" style=\"display: block; padding-left: 26dp; margin-top: 10dp\">Installing " + esc(v.busyName) + "...</div>";
    else if (!v.note.empty()) h += "<div class=\"good\" style=\"display: block; padding-left: 26dp; margin-top: 10dp\">" + esc(v.note) + "</div>";

    const auto& list = m_.mods.mods();
    if (list.empty()) return h + "<p class=\"secondary\" style=\"margin-top: 18dp; padding-left: 26dp\">No mods installed yet.</p>";

    h += "<div class=\"section-label grouphead\" style=\"display: block\">INSTALLED · load order, first to last</div>";
    for (size_t i = 0; i < list.size(); ++i) {
        const ModInfo& m = list[i];
        const ModStatus st = m_.mods.status(m, m_.state.sha256, m_.profile.expectedGameId);
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
        if (!m.patches.empty()) {
            const auto it = v.patchReports->find(m.id);
            int ready = 0;
            std::string lines;
            if (it != v.patchReports->end())
                for (auto& r : it->second) {
                    if (r.ok()) { ++ready; continue; }
                    if (r.state == PatchState::NotBuilt || r.state == PatchState::Skipped) continue;
                    lines += "<div class=\"small warnline\">" + esc(r.target) + ": " + esc(r.detail) + "</div>";
                }
            h += "<div class=\"muted small\">Patches: " + std::to_string(ready) + " of " + std::to_string(m.patches.size()) +
                 " prepared" + (ready < int(m.patches.size()) && lines.empty() ? " (built from your game files when you press PLAY)" : "") + "</div>" + lines;
        }
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
        }
    return h;
}


}  // namespace views
