#include "ui/views/rows.h"

#include <RmlUi/Core/StringUtilities.h>

#include <cstdlib>

namespace views {

namespace {

int toInt(const std::optional<std::string>& s) { return s ? std::atoi(s->c_str()) : 0; }

std::string sizeText(int w, int h) { return std::to_string(w) + " \xC3\x97 " + std::to_string(h); }

// The monitor and resolution rows (settings.json "special" rows).
std::string specialRow(const LauncherModel& m, const SettingRow& row) {
    std::string h;
    if (row.special == "display") {
        if (m.disp.displays.size() < 2) return "";  // one monitor: nothing to choose
        const DisplayInfo& d = m.curDisplay();
        h += rowHtml("display", row.label, row.hint,
                     std::to_string(m.selDisplay + 1) + " \xC2\xB7 " + d.name + " \xC2\xB7 " + sizeText(d.desktop.w, d.desktop.h), false, true);
    } else if (row.special == "resolution") {
        const auto opts = m.resOptions();
        const int w = toInt(m.settings.value("video.width")), h2 = toInt(m.settings.value("video.height"));
        const bool fullscreen = m.videoMode() == "fullscreen";
        const bool custom = !fullscreen && (m.customForced || !inOptions(opts, w, h2));
        std::string val = "Custom\xE2\x80\xA6";
        for (auto& o : opts)
            if (!custom && o.w == w && o.h == h2) val = o.label;
        h += rowHtml("res", row.label, fullscreen ? "Fullscreen offers the sizes your monitor reports" : row.hint, val, false, true);
        if (custom) {
            h += rowHtml("res-w", "Custom width", "In pixels. Hold to go faster", std::to_string(w), false, true);
            h += rowHtml("res-h", "Custom height", "In pixels. Hold to go faster", std::to_string(h2), false, true);
        }
    }
    return h;
}

}  // namespace

std::string esc(const std::string& s) { return Rml::StringUtilities::EncodeRml(s); }

namespace { std::string g_glyphDir; }
void setGlyphDir(const std::string& d) { g_glyphDir = d; for (auto& c : g_glyphDir) if (c == '\\') c = '/'; if (!g_glyphDir.empty() && g_glyphDir.back() != '/') g_glyphDir += '/'; }
std::string glyphHtml(const std::string& stem) { return "<img class=\"glyph\" src=\"" + g_glyphDir + stem + ".png\"/>"; }

std::string rowHtml(const std::string& id, const std::string& label, const std::string& hint, const std::string& val, bool on, bool chevrons,
                    const std::string& apply) {
    std::string h = "<div class=\"setrow\"><div class=\"setlabel\"><div class=\"lbl\">" + esc(label) + "</div>";
    // One line per '\n'; a line starting with '!' is a warning (yellow).
    for (size_t pos = 0; pos < hint.size();) {
        size_t nl = hint.find('\n', pos);
        if (nl == std::string::npos) nl = hint.size();
        std::string line = hint.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) continue;
        const bool warn = line[0] == '!';
        if (warn) line.erase(0, 1);
        h += std::string("<div class=\"small ") + (warn ? "warnline\" style=\"color: #FFD43B" : "muted") + "\">" + esc(line) + "</div>";
    }
    if (!apply.empty()) {
        const char* text = apply == "now" ? "applies now" : apply == "match" ? "next match" : "after restart";
        h += "<div class=\"applybadge applybadge-" + apply + "\">" + text + "</div>";
    }
    h += "</div><button class=\"nav val" + std::string(on ? " on" : "") + "\" data-action=\"cycle\" data-arg=\"" + id + "\" data-key=\"row-" + id +
         "\" data-adjust=\"1\"><span class=\"marker\"/>";
    if (chevrons)
        h += "<span class=\"stepbtn\" data-action=\"step\" data-arg=\"" + id + "@-1\"><span class=\"chev left\"/></span>";
    h += "<span class=\"valtext\">" + esc(val) + "</span>";
    if (chevrons)
        h += "<span class=\"stepbtn\" data-action=\"step\" data-arg=\"" + id + "@1\"><span class=\"chev right\"/></span>";
    return h + "</button></div>";
}

std::string settingsRows(const LauncherModel& m, const std::string& page, bool badges) {
    std::string h;
    for (auto& row : m.settings.page(page)) {
        if (!row.group.empty()) { h += "<div class=\"section-label grouphead\">" + esc(row.group) + "</div>"; continue; }
        if (!row.special.empty()) { h += specialRow(m, row); continue; }
        const int ci = m.settings.currentChoice(row);
        const std::string val = ci >= 0 ? row.choices[ci].label : "Custom";
        std::string hint = row.hint;
        if (row.id == "aspect") {
            const DisplayInfo& d = m.curDisplay();
            const std::string a = aspectName(d.desktop.w, d.desktop.h);
            hint += ". Your screen is " + (a.empty() ? std::to_string(d.desktop.w) + " \xC3\x97 " + std::to_string(d.desktop.h) : a);
        }
        if (row.id == "vsync" && !m.vsyncFixPresent()) hint += "\n!If the game doesn\xE2\x80\x99t start, set Variable refresh to Off.";
        h += rowHtml(row.id, row.label, hint, row.toggle ? (ci == 1 ? "ON" : "OFF") : val, row.toggle && ci == 1, !row.toggle, badges ? row.apply : "");
    }
    return h;
}

}  // namespace views
