// The Controllers tab: ports, device choice, remap rows, dead zone, rumble, menu button swap.
// Everything is saved at once to input_mapping.json (format: docs/INPUT_MAPPING.md).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/identify.h"
#include "ui/App.h"

namespace {

PadFamily familyOf(DeviceKind k) {
    switch (k) {
        case DeviceKind::PlayStation: return PadFamily::PlayStation;
        case DeviceKind::Switch: return PadFamily::Switch;
        case DeviceKind::Generic: return PadFamily::Generic;
        default: return PadFamily::Xbox;
    }
}

constexpr int kRumbleSteps[] = {0, 25, 50, 75, 100};

}  // namespace

std::array<App::PortInfo, InputMap::kPorts> App::resolvePorts() {
    const auto pads = nav_.padList();
    std::vector<bool> taken(pads.size(), false);
    std::array<PortInfo, InputMap::kPorts> out;
    auto fill = [&](PortInfo& p, const Navigation::PadInfo& pad) {
        p.r = {pad.guid, pad.name};
        p.padId = pad.id;
        p.family = familyOf(pad.kind);
    };

    for (int i = 0; i < InputMap::kPorts; ++i) {
        const std::string& dev = input_.device[i];
        if (dev == "auto" || dev == "none") continue;
        if (dev == "keyboard") { out[i].r = {"keyboard", "Keyboard"}; continue; }
        bool found = false;
        for (size_t k = 0; k < pads.size() && !found; ++k)
            if (!taken[k] && pads[k].guid == dev) { fill(out[i], pads[k]); taken[k] = found = true; }
        if (!found) out[i].r = {dev, input_.profile(dev).name};  // remembered but not plugged in
    }
    size_t next = 0;
    for (int i = 0; i < InputMap::kPorts; ++i) {
        if (input_.device[i] != "auto") continue;
        while (next < pads.size() && taken[next]) ++next;
        if (next < pads.size()) { fill(out[i], pads[next]); taken[next] = true; }
    }
    // Remember the device name next to its settings, for people reading the file.
    for (int i = 0; i < InputMap::kPorts; ++i) {
        const std::string pid = InputMap::profileIdFor(input_.device[i], out[i].r.guid);
        if (!pid.empty() && !out[i].r.name.empty()) input_.profile(pid).name = out[i].r.name;
    }
    return out;
}

std::string App::profileIdOf(const std::array<PortInfo, InputMap::kPorts>& info, int port) const {
    return InputMap::profileIdFor(input_.device[port], info[port].r.guid);
}

bool App::saveInput() {
    const auto info = resolvePorts();
    std::array<ResolvedPort, InputMap::kPorts> r;
    for (int i = 0; i < InputMap::kPorts; ++i) r[i] = info[i].r;
    std::string err;
    if (!input_.save(pathToUtf8(inputMapPath()), r, err)) {
        showModal("Could not save controller settings", "Your controller settings could not be written.", err);
        return false;
    }
    return true;
}

std::vector<App::DevOpt> App::deviceOptions(const std::array<PortInfo, InputMap::kPorts>&) {
    std::vector<DevOpt> o = {{"auto", "Automatic"}};
    for (auto& p : nav_.padList()) {
        const bool dup = std::any_of(o.begin(), o.end(), [&](const DevOpt& d) { return d.id == p.guid; });
        if (!dup) o.push_back({p.guid, p.name});
    }
    o.push_back({"keyboard", "Keyboard"});
    o.push_back({"none", "Not used"});
    const std::string& cur = input_.device[ctlPort_];
    if (std::none_of(o.begin(), o.end(), [&](const DevOpt& d) { return d.id == cur; })) {
        const std::string n = input_.profile(cur).name;
        o.insert(o.begin() + 1, {cur, (n.empty() ? "Saved controller" : n) + " (not connected)"});
    }
    return o;
}

void App::beginListen(const std::string& actionId) {
    const auto info = resolvePorts();
    if (profileIdOf(info, ctlPort_).empty()) return;
    listenAction_ = actionId;
    captured_.clear();
    listenCancelled_ = false;
    listenSince_ = SDL_GetTicks();
    render();
}

void App::endListen() {
    listenAction_.clear();
    captured_.clear();
    listenCancelled_ = false;
    refreshHints();
}

// Runs for every SDL event while the Navigation hook is set. Only acts while listening.
bool App::onCapture(const SDL_Event& ev) {
    if (listenAction_.empty()) return false;
    if (!captured_.empty() || listenCancelled_) return ev.type != SDL_EVENT_GAMEPAD_ADDED && ev.type != SDL_EVENT_GAMEPAD_REMOVED;
    const bool kbd = profileIdOf(resolvePorts(), ctlPort_) == "keyboard";
    switch (ev.type) {
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.key == SDLK_ESCAPE) listenCancelled_ = true;
            else if (kbd && !ev.key.repeat) captured_ = std::string("key:") + SDL_GetKeyName(ev.key.key);
            return true;
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: return true;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(ev.gbutton.button));
            if (!kbd && n && std::strcmp(n, "guide") != 0) captured_ = n;  // Guide is reserved for the in-game menu
            return true;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (!kbd && ev.gaxis.value > 20000) {
                if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) captured_ = "lefttrigger";
                else if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) captured_ = "righttrigger";
            }
            return true;
        default: return false;
    }
}

void App::adjustControl(const std::string& what, const std::string& arg, int delta) {
    const auto info = resolvePorts();
    const std::string pid = profileIdOf(info, ctlPort_);

    if (what == "ctl-device") {
        const auto opts = deviceOptions(info);
        int idx = 0;
        for (size_t i = 0; i < opts.size(); ++i)
            if (opts[i].id == input_.device[ctlPort_]) idx = int(i);
        const int n = int(opts.size());
        input_.device[ctlPort_] = opts[(idx + delta + n) % n].id;
    } else if (what == "ctl-deadzone" && !pid.empty()) {
        PadSettings& p = input_.profile(pid);
        const float step = input_.deadzoneStep();
        const int steps = int(std::lround((input_.deadzoneMax() - input_.deadzoneMin()) / step)) + 1;
        int i = int(std::lround((p.deadzone - input_.deadzoneMin()) / step));
        i = ((i + delta) % steps + steps) % steps;
        p.deadzone = input_.deadzoneMin() + step * float(i);
    } else if (what == "ctl-rumble" && !pid.empty()) {
        PadSettings& p = input_.profile(pid);
        int i = 0;
        for (int k = 0; k < 5; ++k)
            if (std::abs(kRumbleSteps[k] - p.rumble) < std::abs(kRumbleSteps[i] - p.rumble)) i = k;
        p.rumble = kRumbleSteps[((i + delta) % 5 + 5) % 5];
    } else if (what == "ctl-stick" && !pid.empty()) {
        const std::string cur = input_.profile(pid).bindings[arg];
        input_.assign(pid, arg, cur == "leftstick" ? "rightstick" : "leftstick");
    } else {
        return;
    }
    saveInput();
    render();
}

std::string App::renderControllers() {
    const auto info = resolvePorts();
    const auto pads = nav_.padList();
    const std::string pid = profileIdOf(info, ctlPort_);
    const std::string dev = input_.device[ctlPort_];

    std::string h = "<div class=\"stack\">";
    // Connected controllers.
    h += "<div class=\"section-label\">CONNECTED</div><p class=\"secondary\">";
    if (pads.empty()) h += "No controller found. The keyboard still works.";
    for (size_t i = 0; i < pads.size(); ++i) h += (i ? " · " : "") + esc(pads[i].name);
    h += "</p>";
    if (!profile_.inputMapSupported)
        h += "<div class=\"note\">Remapping applies after the next game update. Your choices are saved now, and the game uses them as soon as it supports them.</div>";

    h += "<div class=\"section-label\">BUTTON PROMPTS</div>" + renderRows("controls");

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
    for (auto& o : deviceOptions(info))
        if (o.id == dev) devLabel = o.label;
    std::string devHint;
    if (dev == "auto") devHint = info[ctlPort_].r.name.empty() ? "Uses the next controller you connect." : "Using " + info[ctlPort_].r.name + ".";
    else if (dev == "none") devHint = "Nothing is connected to this port.";
    h += row("Controller on port " + std::to_string(ctlPort_ + 1), devHint, adjustBtn("ctl-device", std::to_string(ctlPort_), devLabel));

    const bool kbd = pid == "keyboard";
    if (pid.empty()) {
        // port not used: nothing to set up
    } else if (pid == "default") {
        h += "<p class=\"secondary\" style=\"margin-top: 14dp; padding-left: 26dp\">Connect a controller to set it up. Its buttons are remembered for that controller.</p>";
    } else {
        const PadSettings& p = input_.profile(pid);
        const PadFamily fam = kbd ? PadFamily::Xbox : info[ctlPort_].family;
        const PromptStyle style = kbd ? PromptStyle::Auto : parsePromptStyle(buttonPromptsValue());
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
        for (auto& a : input_.actions()) {
            const auto it = p.bindings.find(a.id);
            const std::string src = it == p.bindings.end() ? "" : it->second;
            if (a.kind == "stick") {
                if (kbd) h += row(a.label, "", "<span class=\"secondary\">" + esc(a.keyDef) + "</span>");
                else h += row(a.label, "Swap the sticks by pressing this.", adjustBtn("ctl-stick", a.id, InputMap::label(src, fam, style)));
                continue;
            }
            const bool listening = listenAction_ == a.id;
            h += row(a.label, "",
                     "<button class=\"nav val" + std::string(listening ? " on" : "") + "\" data-action=\"ctl-bind\" data-arg=\"" + a.id +
                     "\" data-key=\"bind-" + a.id + "\"><span class=\"marker\"/><span class=\"valtext\">" +
                     esc(listening ? "PRESS A BUTTON..." : InputMap::label(src, fam, style)) + "</span></button>");
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
