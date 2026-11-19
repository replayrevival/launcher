#include "core/settings.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "core/identify.h"
#include "core/ini.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string readFile(const fs::path& p, bool& ok) {
    std::ifstream f(p, std::ios::binary);
    ok = bool(f);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool asNumber(const std::string& s, double& out) {
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == 0 && !s.empty();
}

std::optional<std::string> optStr(const json& j) {
    if (j.is_null()) return std::nullopt;
    return j.get<std::string>();
}

}  // namespace

bool Settings::loadSchema(const fs::path& path, std::string& err) {
    bool ok = false;
    const std::string text = readFile(path, ok);
    if (!ok) { err = "Could not open the settings schema: " + pathToUtf8(path); return false; }
    try {
        const json j = json::parse(text);
        for (auto& [id, v] : j.at("keys").items()) {
            SettingKey k;
            k.file = v.value("f", id.substr(0, id.find('.')));   // "f" = the ini file when the id prefix is a section name (audio.*, window.*)
            k.section = v.at("s").get<std::string>();
            k.key = v.at("k").get<std::string>();
            k.def = optStr(v.at("d"));
            keys_[id] = k;
        }
        for (auto& p : j.at("presets")) {
            SettingPreset sp;
            sp.id = p.at("id").get<std::string>();
            sp.name = p.at("name").get<std::string>();
            sp.description = p.value("description", "");
            for (auto& [k, v] : p.at("values").items()) sp.values[k] = v.get<std::string>();
            presets_.push_back(sp);
        }
        for (auto& k : j.value("ignore_for_preset", json::array())) presetIgnore_.insert(k.get<std::string>());
        const json forced = j.value("forced", json::object());
        for (auto& [k, v] : forced.items()) forced_[k] = v.get<std::string>();
        const json mig = j.value("migrate", json::object());
        for (auto& [k, m] : mig.items())
            for (auto& [from, to] : m.items()) migrate_[k][from] = to.get<std::string>();
        const json envByPreset =j.value("env_by_preset", json::object());
        for (auto& [pid, env] : envByPreset.items())
            for (auto& [k, v] : env.items()) presetEnv_[pid][k] = v.get<std::string>();

        for (auto& [pageName, rows] : j.at("pages").items()) {
            for (auto& r : rows) {
                SettingRow row;
                if (r.contains("group")) {
                    row.group = r["group"].get<std::string>();
                    row.hint = r.value("note", "");  // a one-line note under the group heading
                    row.needs = r.value("needs", "");
                } else {
                    row.id = r.at("id").get<std::string>();
                    row.needs = r.value("needs", "");
                    row.label = r.at("label").get<std::string>();
                    row.hint = r.value("hint", "");
                    row.apply = r.value("apply", "restart");
                    const std::string type = r.value("type", "");
                    if (type == "display" || type == "resolution") {
                        row.special = type;
                    } else if (type == "toggle") {
                        row.toggle = true;
                        SettingChoice off{"Off", {}}, on{"On", {}};
                        for (auto& [k, v] : r.at("on").items()) {
                            off.set[k] = std::nullopt;
                            on.set[k] = v.get<std::string>();
                        }
                        row.choices = {off, on};
                    } else {
                        for (auto& c : r.at("choices")) {
                            SettingChoice ch;
                            ch.label = c.at("label").get<std::string>();
                            for (auto& [k, v] : c.at("set").items()) ch.set[k] = optStr(v);
                            row.choices.push_back(ch);
                        }
                    }
                    for (auto& c : row.choices)
                        for (auto& [k, v] : c.set)
                            if (!keys_.count(k)) { err = "settings.json: unknown key " + k; return false; }
                }
                allPages_[pageName].push_back(row);
            }
        }
        rebuildPages();
        return true;
    } catch (const std::exception& e) {
        err = std::string("The settings schema is not valid: ") + e.what();
        return false;
    }
}

const std::vector<SettingRow>& Settings::page(const std::string& name) const {
    static const std::vector<SettingRow> none;
    auto it = pages_.find(name);
    return it == pages_.end() ? none : it->second;
}

void Settings::rebuildPages() {
    pages_.clear();
    for (auto& [name, rows] : allPages_) {
        auto& out = pages_[name];
        for (auto& r : rows)
            if (r.needs.empty() || caps_.count(r.needs)) out.push_back(r);
    }
}

void Settings::setCapabilities(std::set<std::string> caps) {
    caps_ = std::move(caps);
    rebuildPages();
}

std::vector<std::string> Settings::fileIds() const {
    std::set<std::string> s;
    for (auto& [id, k] : keys_) s.insert(k.file);
    return {s.begin(), s.end()};
}

void Settings::load(const fs::path& dir) {
    values_.clear();
    reload(dir);
}

// Re-reads the ini files. A file that cannot be read keeps what we have in memory, so a missing
// file never turns the user's choices back into defaults.
void Settings::reload(const fs::path& dir) {
    for (const auto& fid : fileIds()) {
        bool ok = false;
        const std::string text = readFile(dir / fileName(fid), ok);
        if (!ok) continue;
        Ini ini;
        ini.parse(text);
        for (auto& [id, k] : keys_) {
            if (k.file != fid) continue;
            values_.erase(id);
            if (auto v = ini.get(k.section, k.key); v && !v->empty()) values_[id] = *v;
        }
    }
    for (auto& [id, m] : migrate_) {
        auto it = values_.find(id);
        if (it == values_.end()) continue;
        auto to = m.find(it->second);
        if (to != m.end()) it->second = to->second;
    }
    enforce();
}

void Settings::enforce() {
    for (auto& [k, v] : forced_) values_[k] = v;
}

void Settings::set(const std::string& id, const std::optional<std::string>& v) {
    if (v) values_[id] = *v;
    else values_.erase(id);
    enforce();
}

bool Settings::save(const fs::path& dir, std::string& err) const {
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (const auto& fid : fileIds()) {
        const fs::path path = dir / fileName(fid);
        bool ok = false;
        std::string text = readFile(path, ok);
        if (!ok) text = "; " + fileName(fid) + " - written by the launcher. Keys that are not listed use the game's own defaults.\n";
        Ini ini;
        ini.parse(text);
        for (auto& [id, k] : keys_) {
            if (k.file != fid) continue;
            auto it = values_.find(id);
            if (it != values_.end()) ini.set(k.section, k.key, it->second);
            else ini.unset(k.section, k.key);
        }
        const fs::path tmp = path.string() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << ini.text();
            if (!f) { err = "Could not write " + pathToUtf8(tmp); return false; }
        }
        fs::rename(tmp, path, ec);
        if (ec) {
            fs::remove(path, ec);
            fs::rename(tmp, path, ec);
            if (ec) { err = "Could not replace " + pathToUtf8(path) + ": " + ec.message(); return false; }
        }
    }
    return true;
}

std::optional<std::string> Settings::effective(const std::string& key) const {
    auto it = values_.find(key);
    if (it != values_.end()) return it->second;
    return keys_.at(key).def;
}

bool Settings::sameValue(const std::optional<std::string>& a, const std::optional<std::string>& b) {
    if (!a || !b) return !a && !b;
    double x, y;
    if (asNumber(*a, x) && asNumber(*b, y)) return std::fabs(x - y) < 1e-6;
    return *a == *b;
}

int Settings::currentChoice(const SettingRow& row) const {
    for (size_t i = 0; i < row.choices.size(); ++i) {
        bool match = true;
        for (auto& [k, v] : row.choices[i].set) {
            // A toggle's "off" is the key's default, whatever that default is.
            std::optional<std::string> want = v;
            if (!want && row.toggle) want = keys_.at(k).def;
            if (!sameValue(effective(k), want)) { match = false; break; }
        }
        if (match) return int(i);
    }
    return -1;
}

void Settings::choose(const SettingRow& row, int index) {
    if (index < 0 || index >= int(row.choices.size())) return;
    for (auto& [k, v] : row.choices[index].set) {
        if (v) values_[k] = *v;
        else values_.erase(k);
    }
}

void Settings::cycle(const SettingRow& row, int delta) {
    const int n = int(row.choices.size());
    if (!n) return;
    int cur = currentChoice(row);
    if (cur < 0) cur = delta > 0 ? -1 : 0;
    choose(row, ((cur + delta) % n + n) % n);
}

const SettingPreset* Settings::preset(const std::string& id) const {
    for (auto& p : presets_)
        if (p.id == id) return &p;
    return nullptr;
}

std::map<std::string, std::string> Settings::presetEnv(const std::string& id) const {
    auto it = presetEnv_.find(id);
    return it == presetEnv_.end() ? std::map<std::string, std::string>{} : it->second;
}

std::string Settings::activePreset() const {
    for (auto& p : presets_) {
        bool match = true;
        for (auto& [id, k] : keys_) {
            if (presetIgnore_.count(id)) continue;
            auto pv = p.values.find(id);
            auto pd = presetDefaults_.find(id);
            const std::optional<std::string> want = pv != p.values.end()   ? std::optional<std::string>(pv->second)
                                                    : pd != presetDefaults_.end() ? std::optional<std::string>(pd->second)
                                                                                  : k.def;
            if (!sameValue(effective(id), want)) { match = false; break; }
        }
        if (match) return p.id;
    }
    return "";
}

void Settings::applyPreset(const std::string& id) {
    const SettingPreset* p = preset(id);
    if (!p) return;
    std::map<std::string, std::string> keep;  // screen choices survive a preset change
    for (auto& k : presetIgnore_)
        if (auto it = values_.find(k); it != values_.end()) keep[k] = it->second;
    values_.clear();
    for (auto& [k, v] : presetDefaults_) values_[k] = v;
    for (auto& [k, v] : p->values) values_[k] = v;
    for (auto& [k, v] : keep) values_[k] = v;
    enforce();
}

void Settings::setPresetDefault(const std::string& key, const std::optional<std::string>& v) {
    if (v) presetDefaults_[key] = *v;
    else presetDefaults_.erase(key);
}
