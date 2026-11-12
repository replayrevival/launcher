// The Controllers tab: ports, device choice, remap rows, dead zone, rumble, menu button swap.
// Everything is saved at once to input_mapping.json (format: docs/INPUT_MAPPING.md).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/identify.h"
#include "ui/App.h"

std::array<App::PortInfo, InputMap::kPorts> App::resolvePorts() {
    std::vector<PadDesc> pads;
    for (auto& p : nav_.padList()) pads.push_back({p.guid, p.name, p.kind, uint32_t(p.id)});
    ctl_.setPads(std::move(pads));
    return ctl_.resolvePorts();
}

std::string App::profileIdOf(const std::array<PortInfo, InputMap::kPorts>& info, int port) const { return ctl_.profileIdOf(info, port); }

bool App::saveInput() {
    resolvePorts();  // the connected pads decide the names written next to the settings
    std::string err;
    if (!ctl_.save(err)) {
        showModal("Could not save controller settings", "Your controller settings could not be written.", err);
        return false;
    }
    return true;
}

std::vector<App::DevOpt> App::deviceOptions(const std::array<PortInfo, InputMap::kPorts>&) { return ctl_.deviceOptions(); }

void App::beginListen(const std::string& actionId) {
    resolvePorts();
    if (!ctl_.beginListen(actionId, SDL_GetTicks())) return;
    captured_.clear();
    listenCancelled_ = false;
    render();
}

void App::endListen() {
    ctl_.endListen();
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
    resolvePorts();
    std::string err;
    if (!ctl_.adjust(what, arg, delta, err)) return;
    if (!err.empty()) showModal("Could not save controller settings", "Your controller settings could not be written.", err);
    render();
}

std::string App::renderControllers() {
    resolvePorts();
    return views::controllersPage(model_, ctl_.viewState());
}
