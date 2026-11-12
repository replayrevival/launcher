#include "ui/views/controllers.h"

#include <cmath>

#include "ui/views/rows.h"

namespace views {

std::string controllersPage(const LauncherModel& model, const ControllersViewState& v) {
    const InputMap& input_ = model.input;
    const int ctlPort_ = v.port;
    const auto& pads = v.padNames;
    const std::string& pid = v.profileId;
    const std::string dev = input_.device[ctlPort_];

    std::string h = "<div class=\"stack\">";
    // Connected controllers.
    h += "<div class=\"section-label\">CONNECTED</div><p class=\"secondary\">";
    if (pads.empty()) h += "No controller found. The keyboard still works.";
    for (size_t i = 0; i < pads.size(); ++i) h += (i ? " · " : "") + esc(pads[i]);
    h += "</p>";
    if (!model.profile.inputMapSupported)
        h += "<div class=\"note\">Remapping applies after the next game update. Your choices are saved now, and the game uses them as soon as it supports them.</div>";

    h += "<div class=\"section-label\">BUTTON PROMPTS</div>" + settingsRows(model, "controls");

    h += "<div class=\"section-label\">PORT</div><div class=\"row indent\">";
    for (int i = 0; i < InputMap::kPorts; ++i)
        h += std::string("<button class=\"nav") + (i == ctlPort_ ? " selected" : "") + "\" data-action=\"ctl-port\" data-arg=\"" +
             std::to_string(i) + "\" data-key=\"ctl-port-" + std::to_string(i) + "\"><span class=\"marker\"/>PORT " + std::to_string(i + 1) + "</button>";
    h += "</div>";

    auto adjustBtn = [&](const std::string& kind, const std::string& arg, const std::string& text, bool on = false) {
        const std::string st = "data-action=\"ctl-step\" data-arg=\"" + kind + "@" + arg + "@";
        return "<button class=\"nav val" + std::string(on ? " on" : "") + "\" data-action=\"" + kind + "\" data-arg=\"" + arg +
               "\" data-key=\"" + kind + "-" + arg + "\" data-adjust=\"" + kind + "\"><span class=\"marker\"/>"
               "<span class=\"stepbtn\" " + st + "-1\"><span class=\"chev left\"/></span>"
               "<span class=\"valtext\">" + esc(text) + "</span>"
               "<span class=\"stepbtn\" " + st + "1\"><span class=\"chev right\"/></span></button>";
    };
    auto row = [&](const std::string& label, const std::string& hint, const std::string& control) {
        std::string r = "<div class=\"setrow\"><div class=\"setlabel\"><div class=\"lbl\">" + esc(label) + "</div>";
        if (!hint.empty()) r += "<div class=\"muted small\">" + esc(hint) + "</div>";
        return r + "</div>" + control + "</div>";
    };

    std::string devLabel = "Automatic";
    for (auto& o : v.devices)
        if (o.id == dev) devLabel = o.label;
    std::string devHint;
    if (dev == "auto") devHint = v.portName.empty() ? "Uses the next controller you connect." : "Using " + v.portName + ".";
    else if (dev == "none") devHint = "Nothing is connected to this port.";
    h += row("Controller on port " + std::to_string(ctlPort_ + 1), devHint, adjustBtn("ctl-device", std::to_string(ctlPort_), devLabel));

    const bool kbd = pid == "keyboard";
    if (pid.empty()) {
        // port not used: nothing to set up
    } else if (pid == "default") {
        h += "<p class=\"secondary\" style=\"margin-top: 14dp; padding-left: 26dp\">Connect a controller to set it up. Its buttons are remembered for that controller.</p>";
    } else {
        const PadSettings& p = v.pad;
        const PadFamily fam = kbd ? PadFamily::Xbox : v.portFamily;
        const PromptStyle style = kbd ? PromptStyle::Auto : parsePromptStyle(model.buttonPrompts());
        h += "<p class=\"muted small\" style=\"padding-left: 26dp; margin-top: 6dp\">" +
             std::string(kbd ? "Keyboard controls." : "These settings are remembered for this controller, whichever port it is on.") + "</p>";
        if (!kbd) {
            h += row("Stick dead zone", "Ignore small stick movements near the centre.",
                     adjustBtn("ctl-deadzone", "dz", std::to_string(int(std::lround(p.deadzone * 100.f))) + "%"));
            h += row("Rumble", "How strongly the controller vibrates.", adjustBtn("ctl-rumble", "rumble", p.rumble == 0 ? "OFF" : std::to_string(p.rumble) + "%"));
            h += "<div class=\"row filerow\"><span class=\"muted small\">Check the vibration:</span>"
                 "<button class=\"nav\" data-action=\"ctl-test\" data-key=\"ctl-test\"><span class=\"marker\"/>TEST RUMBLE</button></div>";
        }
        h += "<div class=\"section-label grouphead\">BUTTONS · select one, then press the new button</div>";
        auto bindGlyph = [&](const std::string& src) {
            const std::string g = glyphFile(src, fam, style);
            return g.empty() ? std::string() : views::glyphHtml(g);
        };
        for (auto& a : input_.actions()) {
            const auto it = p.bindings.find(a.id);
            const std::string src = it == p.bindings.end() ? "" : it->second;
            if (a.kind == "stick") {
                if (kbd) h += row(a.label, "", "<span class=\"secondary\">" + esc(a.keyDef) + "</span>");
                else h += row(a.label, "Swap the sticks by pressing this.", adjustBtn("ctl-stick", a.id, InputMap::label(src, fam, style)));
                continue;
            }
            const bool listening = v.listenAction == a.id;
            h += row(a.label, "",
                     "<button class=\"nav val" + std::string(listening ? " on" : "") + "\" data-action=\"ctl-bind\" data-arg=\"" + a.id +
                     "\" data-key=\"bind-" + a.id + "\"><span class=\"marker\"/><span class=\"valtext\">" +
                     (listening ? "" : bindGlyph(src)) + esc(listening ? "PRESS A BUTTON..." : InputMap::label(src, fam, style)) +
                     "</span></button>");
        }
        h += "<div class=\"row filerow\"><span class=\"muted small\">Go back to the original layout:</span>"
             "<button class=\"nav\" data-action=\"ctl-reset\" data-key=\"ctl-reset\"><span class=\"marker\"/>RESET BUTTONS</button></div>";
    }

    h += "<div class=\"section-label grouphead\">MENUS</div>";
    h += row("Swap confirm and back", "Menus use the right-hand button to confirm (Nintendo layout).",
             "<button class=\"nav val" + std::string(input_.swapConfirmBack ? " on" : "") +
                 "\" data-action=\"ctl-swap\" data-key=\"ctl-swap\"><span class=\"marker\"/><span class=\"valtext\">" +
                 (input_.swapConfirmBack ? "ON" : "OFF") + "</span></button>");
    return h + "</div>";
}

}  // namespace views
