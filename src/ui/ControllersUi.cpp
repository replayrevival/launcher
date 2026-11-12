#include "ui/ControllersUi.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/identify.h"

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

std::array<ControllersUi::PortInfo, InputMap::kPorts> ControllersUi::resolvePorts() {
    InputMap& input = m_->input;
    std::vector<bool> taken(pads_.size(), false);
    std::array<PortInfo, InputMap::kPorts> out;
    auto fill = [&](PortInfo& p, const PadDesc& pad) {
        p.r = {pad.guid, pad.name};
        p.padId = pad.id;
        p.family = familyOf(pad.kind);
    };

    for (int i = 0; i < InputMap::kPorts; ++i) {
        const std::string& dev = input.device[i];
        if (dev == "auto" || dev == "none") continue;
        if (dev == "keyboard") { out[i].r = {"keyboard", "Keyboard"}; continue; }
        bool found = false;
        for (size_t k = 0; k < pads_.size() && !found; ++k)
            if (!taken[k] && pads_[k].guid == dev) { fill(out[i], pads_[k]); taken[k] = found = true; }
        if (!found) out[i].r = {dev, input.profile(dev).name};  // remembered but not plugged in
    }
    size_t next = 0;
    for (int i = 0; i < InputMap::kPorts; ++i) {
        if (input.device[i] != "auto") continue;
        while (next < pads_.size() && taken[next]) ++next;
        if (next < pads_.size()) { fill(out[i], pads_[next]); taken[next] = true; }
    }
    // Remember the device name next to its settings, for people reading the file.
    for (int i = 0; i < InputMap::kPorts; ++i) {
        const std::string pid = InputMap::profileIdFor(input.device[i], out[i].r.guid);
        if (!pid.empty() && !out[i].r.name.empty()) input.profile(pid).name = out[i].r.name;
    }
    return out;
}

std::string ControllersUi::profileIdOf(const std::array<PortInfo, InputMap::kPorts>& info, int p) const {
    return InputMap::profileIdFor(m_->input.device[p], info[p].r.guid);
}

std::vector<ControllersUi::DevOpt> ControllersUi::deviceOptions() {
    std::vector<DevOpt> o = {{"auto", "Automatic"}};
    for (auto& p : pads_) {
        const bool dup = std::any_of(o.begin(), o.end(), [&](const DevOpt& d) { return d.id == p.guid; });
        if (!dup) o.push_back({p.guid, p.name});
    }
    o.push_back({"keyboard", "Keyboard"});
    o.push_back({"none", "Not used"});
    const std::string& cur = m_->input.device[port];
    if (std::none_of(o.begin(), o.end(), [&](const DevOpt& d) { return d.id == cur; })) {
        const std::string n = m_->input.profile(cur).name;
        o.insert(o.begin() + 1, {cur, (n.empty() ? "Saved controller" : n) + " (not connected)"});
    }
    return o;
}

views::ControllersViewState ControllersUi::viewState() {
    const auto info = resolvePorts();
    views::ControllersViewState v;
    v.port = port;
    for (auto& p : pads_) v.padNames.push_back(p.name);
    v.profileId = profileIdOf(info, port);
    v.portName = info[port].r.name;
    v.portFamily = info[port].family;
    for (auto& d : deviceOptions()) v.devices.push_back({d.id, d.label});
    if (!v.profileId.empty() && v.profileId != "default") v.pad = m_->input.profile(v.profileId);
    v.listenAction = listenAction;
    return v;
}

bool ControllersUi::save(std::string& err) {
    const auto info = resolvePorts();
    std::array<ResolvedPort, InputMap::kPorts> r;
    for (int i = 0; i < InputMap::kPorts; ++i) r[i] = info[i].r;
    return m_->input.save(pathToUtf8(m_->inputMapPath()), r, err);
}

bool ControllersUi::adjust(const std::string& what, const std::string& arg, int delta, std::string& err) {
    InputMap& input = m_->input;
    const auto info = resolvePorts();
    const std::string pid = profileIdOf(info, port);

    if (what == "ctl-device") {
        const auto opts = deviceOptions();
        int idx = 0;
        for (size_t i = 0; i < opts.size(); ++i)
            if (opts[i].id == input.device[port]) idx = int(i);
        const int n = int(opts.size());
        input.device[port] = opts[(idx + delta + n) % n].id;
    } else if (what == "ctl-deadzone" && !pid.empty()) {
        PadSettings& p = input.profile(pid);
        const float step = input.deadzoneStep();
        const int steps = int(std::lround((input.deadzoneMax() - input.deadzoneMin()) / step)) + 1;
        int i = int(std::lround((p.deadzone - input.deadzoneMin()) / step));
        i = ((i + delta) % steps + steps) % steps;
        p.deadzone = input.deadzoneMin() + step * float(i);
    } else if (what == "ctl-rumble" && !pid.empty()) {
        PadSettings& p = input.profile(pid);
        int i = 0;
        for (int k = 0; k < 5; ++k)
            if (std::abs(kRumbleSteps[k] - p.rumble) < std::abs(kRumbleSteps[i] - p.rumble)) i = k;
        p.rumble = kRumbleSteps[((i + delta) % 5 + 5) % 5];
    } else if (what == "ctl-stick" && !pid.empty()) {
        const std::string cur = input.profile(pid).bindings[arg];
        input.assign(pid, arg, cur == "leftstick" ? "rightstick" : "leftstick");
    } else {
        return false;
    }
    save(err);
    return true;
}

bool ControllersUi::adjustFromStep(const std::string& arg, std::string& err) {
    const size_t at = arg.rfind('@');
    if (at == std::string::npos || at == 0) return false;
    const size_t at1 = arg.rfind('@', at - 1);
    if (at1 == std::string::npos || at1 >= at) return false;
    return adjust(arg.substr(0, at1), arg.substr(at1 + 1, at - at1 - 1), std::atoi(arg.c_str() + at + 1) < 0 ? -1 : +1, err);
}

void ControllersUi::selectPort(int p) { port = std::clamp(p, 0, InputMap::kPorts - 1); }

void ControllersUi::resetBindings(std::string& err) {
    const std::string pid = profileIdOf(resolvePorts(), port);
    if (pid.empty()) return;
    m_->input.resetBindings(pid);
    save(err);
}

void ControllersUi::toggleSwapConfirmBack(std::string& err) {
    m_->input.swapConfirmBack = !m_->input.swapConfirmBack;
    save(err);
}

bool ControllersUi::beginListen(const std::string& actionId, uint64_t nowMs) {
    if (profileIdOf(resolvePorts(), port).empty()) return false;
    listenAction = actionId;
    listenSince = nowMs;
    return true;
}

bool ControllersUi::applyCapture(const std::string& source, std::string& err) {
    const std::string actionId = listenAction;
    listenAction.clear();
    if (source.empty() || actionId.empty()) return false;
    const std::string pid = profileIdOf(resolvePorts(), port);
    if (pid.empty() || !m_->input.assign(pid, actionId, source)) return false;
    return save(err), true;
}
