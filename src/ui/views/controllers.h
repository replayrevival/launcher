#pragma once
// The Controllers page (ports, device, dead zone, rumble, remap rows) as RmlUi markup. Shared by the launcher (App) and the
// planned in-game overlay. No SDL: the host resolves the pads and ports and passes the result in ControllersViewState; actions
// are `data-action` attributes (ctl-port, ctl-step, ctl-bind, ctl-test, ctl-reset, ctl-swap, ...) the host handles.
#include <string>
#include <vector>

#include "core/input_map.h"
#include "core/launcher_model.h"

namespace views {

struct ControllersViewState {
    struct Device { std::string id, label; };
    int port = 0;                           // selected port, 0-based
    std::vector<std::string> padNames;      // connected controllers
    std::string profileId;                  // profile of the selected port ("" = port not used, "default" = nothing attached, "keyboard")
    std::string portName;                   // name of the pad on the selected port ("" = none yet)
    PadFamily portFamily = PadFamily::Xbox;
    std::vector<Device> devices;            // choices for "Controller on port N"
    PadSettings pad;                        // settings of `profileId` (a copy, so the page stays const)
    std::string listenAction;               // control waiting for a button press ("" = none)
};

std::string controllersPage(const LauncherModel& model, const ControllersViewState& state);

}  // namespace views
