#pragma once
// The logic behind the Controllers page, shared by the launcher (App) and the in-game overlay: which controller is on which
// port, device choices, dead zone / rumble / stick swap, the remap "press a button" state, saving input_mapping.json.
// No SDL and no window: the host tells it which controllers are connected (setPads) and hands it the button that was pressed.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/input_map.h"
#include "core/launcher_model.h"
#include "ui/NavCore.h"
#include "ui/views/controllers.h"

struct PadDesc {
    std::string guid, name;
    DeviceKind kind = DeviceKind::Generic;
    uint32_t id = 0;  // the host's own handle for the pad (SDL joystick id in the launcher); 0 = none
};

class ControllersUi {
public:
    ControllersUi() = default;
    explicit ControllersUi(LauncherModel& m) : m_(&m) {}
    void attach(LauncherModel& m) { m_ = &m; }

    int port = 0;               // selected port, 0-based
    std::string listenAction;   // control waiting for a button press ("" = not listening)
    uint64_t listenSince = 0;   // ms clock value when listening began
    static constexpr uint64_t kListenTimeoutMs = 6000;

    void setPads(std::vector<PadDesc> pads) { pads_ = std::move(pads); }
    const std::vector<PadDesc>& pads() const { return pads_; }

    struct PortInfo {
        ResolvedPort r;                 // what the port resolved to (empty = nothing attached)
        uint32_t padId = 0;
        PadFamily family = PadFamily::Xbox;
    };
    struct DevOpt { std::string id, label; };

    std::array<PortInfo, InputMap::kPorts> resolvePorts();
    std::string profileIdOf(const std::array<PortInfo, InputMap::kPorts>& info, int p) const;
    std::vector<DevOpt> deviceOptions();
    views::ControllersViewState viewState();

    // Changes a row of the page by `delta` steps: what = ctl-device | ctl-deadzone | ctl-rumble | ctl-stick. Saves the file.
    // Returns false when `what` is not one of these; `err` is set when saving failed.
    bool adjust(const std::string& what, const std::string& arg, int delta, std::string& err);
    // "<kind>@<arg>@<dir>" as produced by the page's chevrons.
    bool adjustFromStep(const std::string& arg, std::string& err);
    void selectPort(int p);
    void resetBindings(std::string& err);   // selected port's controller back to the default layout
    void toggleSwapConfirmBack(std::string& err);

    // Remapping: beginListen needs a controller on the selected port. applyCapture(source) binds the pressed source (an SDL
    // gamepad button name like "leftshoulder", "lefttrigger"/"righttrigger", or "key:<name>" for the keyboard) and stops listening.
    bool beginListen(const std::string& actionId, uint64_t nowMs);
    void endListen() { listenAction.clear(); }
    bool applyCapture(const std::string& source, std::string& err);
    bool listenTimedOut(uint64_t nowMs) const { return !listenAction.empty() && nowMs - listenSince > kListenTimeoutMs; }
    bool keyboardPort() { return profileIdOf(resolvePorts(), port) == "keyboard"; }

    bool save(std::string& err);

private:
    LauncherModel* m_ = nullptr;
    std::vector<PadDesc> pads_;
};
