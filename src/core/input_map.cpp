#include "core/input_map.h"

#include <nlohmann/json.hpp>

#include "core/identify.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using nlohmann::json;

bool InputMap::loadSchema(const std::string& path, std::string& err) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    if (!f) { err = "Could not open the controls list: " + path; return false; }
    try {
        const json j = json::parse(f);
        const json dz = j.value("deadzone", json::object());
        dzDefault_ = dz.value("default", 0.15f);
        dzMin_ = dz.value("min", 0.0f);
        dzMax_ = dz.value("max", 0.5f);
        dzStep_ = dz.value("step", 0.05f);
        for (const auto& a : j.at("actions")) {
            ControlAction c;
            c.id = a.at("id").get<std::string>();
            c.label = a.at("label").get<std::string>();
            c.kind = a.value("kind", "button");
            c.def = a.at("default").get<std::string>();
            c.keyDef = a.value("key_default", "");
            actions_.push_back(std::move(c));
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("The controls list is not valid: ") + e.what();
        return false;
    }
}

const ControlAction* InputMap::action(const std::string& id) const {
    for (auto& a : actions_)
        if (a.id == id) return &a;
    return nullptr;
}

std::string InputMap::profileIdFor(const std::string& dev, const std::string& guid) {
    if (dev == "none") return "";
    if (dev == "keyboard") return "keyboard";
    if (dev == "auto") return guid.empty() ? "default" : guid;
    return dev;
}

std::string InputMap::defaultName(const std::string& id) {
    if (id == "keyboard") return "Keyboard";
    if (id == "default") return "Any controller";
    return "";
}

PadSettings InputMap::makeDefault(const std::string& id) const {
    PadSettings p;
    p.deadzone = dzDefault_;
    p.name = defaultName(id);
    for (auto& a : actions_) p.bindings[a.id] = id == "keyboard" ? "key:" + a.keyDef : a.def;
    return p;
}

PadSettings& InputMap::profile(const std::string& id) {
    auto it = profiles_.find(id);
    if (it == profiles_.end()) it = profiles_.emplace(id, makeDefault(id)).first;
    return it->second;
}

void InputMap::resetBindings(const std::string& id) {
    PadSettings d = makeDefault(id);
    PadSettings& p = profile(id);
    p.bindings = d.bindings;
}

bool InputMap::assign(const std::string& id, const std::string& actionId, const std::string& source) {
    const ControlAction* a = action(actionId);
    if (!a) return false;
    const bool keyboard = id == "keyboard";
    if (a->kind == "stick") {
        if (keyboard || (source != "leftstick" && source != "rightstick")) return false;
    } else {
        if (keyboard != isKeySource(source)) return false;
        if (source == "guide" || source == "key:Escape") return false;  // reserved (overlay / cancel)
    }
    PadSettings& p = profile(id);
    const std::string old = p.bindings[actionId];
    for (auto& [other, src] : p.bindings) {
        if (other == actionId || src != source) continue;
        const ControlAction* o = action(other);
        if (o && (o->kind == "stick") != (a->kind == "stick")) continue;
        src = old;  // the other action takes over what this one used
    }
    p.bindings[actionId] = source;
    return true;
}

bool InputMap::load(const std::string& file) {
    std::ifstream f(pathFromUtf8(file), std::ios::binary);
    if (!f) return false;
    try {
        const json j = json::parse(f);
        swapConfirmBack = j.value("menu_swap_confirm_back", false);
        const json devs = j.value("ports", json::array());
        for (size_t i = 0; i < devs.size() && i < size_t(kPorts); ++i) device[i] = devs[i].value("device", "auto");
        const json profs = j.value("profiles", json::object());
        for (auto it = profs.begin(); it != profs.end(); ++it) {
            PadSettings& p = profile(it.key());
            const json& v = it.value();
            p.name = v.value("name", p.name);
            p.deadzone = v.value("deadzone", p.deadzone);
            p.rumble = v.value("rumble", p.rumble);
            const json b = v.value("bindings", json::object());
            for (auto bi = b.begin(); bi != b.end(); ++bi)
                if (action(bi.key())) p.bindings[bi.key()] = bi.value().get<std::string>();
        }
        return true;
    } catch (...) {
        return false;  // damaged file = start from defaults
    }
}

namespace {
json settingsJson(const PadSettings& p) {
    json b = json::object();
    for (auto& [k, v] : p.bindings) b[k] = v;
    return {{"name", p.name}, {"deadzone", std::round(double(p.deadzone) * 100.0) / 100.0}, {"rumble", p.rumble}, {"bindings", b}};
}
}  // namespace

bool InputMap::save(const std::string& file, const std::array<ResolvedPort, kPorts>& resolved, std::string& err) const {
    auto self = const_cast<InputMap*>(this);  // profile() creates defaults on first use; that is fine for a save
    json ports = json::array();
    for (int i = 0; i < kPorts; ++i) {
        const std::string pid = profileIdFor(device[i], resolved[i].guid);
        json p = {{"port", i + 1}, {"device", device[i]}, {"guid", resolved[i].guid}, {"name", resolved[i].name}, {"profile", pid}};
        if (!pid.empty()) p["settings"] = settingsJson(self->profile(pid));
        ports.push_back(std::move(p));
    }
    json profs = json::object();
    for (auto& [id, p] : profiles_) profs[id] = settingsJson(p);
    const json j = {{"format", 1}, {"menu_swap_confirm_back", swapConfirmBack}, {"ports", ports}, {"profiles", profs}};

    const fs::path path = pathFromUtf8(file);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(2);
        if (!f) { err = "Could not save the controller settings: " + tmp.string(); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) { err = "Could not save the controller settings: " + ec.message(); return false; }
    }
    return true;
}

PromptStyle parsePromptStyle(const std::string& v) {
    if (v == "xbox") return PromptStyle::Xbox;
    if (v == "xbox_original") return PromptStyle::XboxOriginal;
    if (v == "playstation") return PromptStyle::PlayStation;
    return PromptStyle::Auto;
}

std::string glyphFile(const std::string& s, PadFamily f, PromptStyle style) {
    if (InputMap::isKeySource(s)) return "";
    if (style == PromptStyle::Xbox || style == PromptStyle::XboxOriginal) f = PadFamily::Xbox;
    if (style == PromptStyle::PlayStation) f = PadFamily::PlayStation;
    if (f == PadFamily::Xbox) {
        static const std::map<std::string, std::string> x = {
            {"a", "xbox_button_a"}, {"b", "xbox_button_b"}, {"x", "xbox_button_x"}, {"y", "xbox_button_y"},
            {"back", "xbox_button_back"}, {"start", "xbox_button_start"}, {"leftshoulder", "xbox_lb"}, {"rightshoulder", "xbox_rb"},
            {"lefttrigger", "xbox_lt"}, {"righttrigger", "xbox_rt"}, {"leftstick", "xbox_ls"}, {"rightstick", "xbox_rs"},
            {"dpup", "xbox_dpad_up"}, {"dpdown", "xbox_dpad_down"}, {"dpleft", "xbox_dpad_left"}, {"dpright", "xbox_dpad_right"}};
        if (style == PromptStyle::XboxOriginal) {
            if (s == "leftshoulder") return "ours_xbox_black";  // Black / White: not in the Kenney set; our own art
            if (s == "rightshoulder") return "ours_xbox_white";
            if (s == "a" || s == "b" || s == "x" || s == "y") return "xbox_button_color_" + s;  // coloured, like the original pad
        }
        auto it = x.find(s);
        return it == x.end() ? "" : it->second;
    }
    if (f == PadFamily::PlayStation) {
        static const std::map<std::string, std::string> p = {
            {"a", "playstation_button_cross"}, {"b", "playstation_button_circle"}, {"x", "playstation_button_square"},
            {"y", "playstation_button_triangle"}, {"back", "playstation5_button_create"}, {"start", "playstation5_button_options"},
            {"leftshoulder", "playstation_trigger_l1"}, {"rightshoulder", "playstation_trigger_r1"},
            {"lefttrigger", "playstation_trigger_l2"}, {"righttrigger", "playstation_trigger_r2"},
            {"leftstick", "playstation_button_l3"}, {"rightstick", "playstation_button_r3"},
            {"dpup", "playstation_dpad_up"}, {"dpdown", "playstation_dpad_down"}, {"dpleft", "playstation_dpad_left"},
            {"dpright", "playstation_dpad_right"}};
        auto it = p.find(s);
        return it == p.end() ? "" : it->second;
    }
    return "";
}

std::string InputMap::label(const std::string& s, PadFamily f, PromptStyle style) {
    if (isKeySource(s)) return s.substr(4);
    if (style == PromptStyle::XboxOriginal) {
        if (s == "leftshoulder") return "Black";
        if (s == "rightshoulder") return "White";
    }
    if (style == PromptStyle::Xbox || style == PromptStyle::XboxOriginal) f = PadFamily::Xbox;
    if (style == PromptStyle::PlayStation) f = PadFamily::PlayStation;
    struct Row { const char* src; const char* xbox; const char* ps; const char* sw; };
    static const Row t[] = {
        {"a", "A", "Cross", "B"},          {"b", "B", "Circle", "A"},          {"x", "X", "Square", "Y"},
        {"y", "Y", "Triangle", "X"},       {"back", "Back", "Create", "-"},    {"start", "Start", "Options", "+"},
        {"leftshoulder", "LB", "L1", "L"}, {"rightshoulder", "RB", "R1", "R"}, {"lefttrigger", "LT", "L2", "ZL"},
        {"righttrigger", "RT", "R2", "ZR"}, {"leftstick", "Left stick", "L3", "Left stick"},
        {"rightstick", "Right stick", "R3", "Right stick"}, {"dpup", "D-pad up", "D-pad up", "D-pad up"},
        {"dpdown", "D-pad down", "D-pad down", "D-pad down"}, {"dpleft", "D-pad left", "D-pad left", "D-pad left"},
        {"dpright", "D-pad right", "D-pad right", "D-pad right"}};
    for (auto& r : t)
        if (s == r.src) return f == PadFamily::PlayStation ? r.ps : f == PadFamily::Switch ? r.sw : r.xbox;
    return s;
}
