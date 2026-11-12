#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

struct SettingChoice {
    std::string label;
    std::map<std::string, std::optional<std::string>> set;  // key id -> value (nullopt = remove the key)
};

struct SettingRow {
    std::string id, label, hint;
    std::string group;  // non-empty: this entry is a group header, nothing else is used
    bool toggle = false;
    std::string special;  // "display" / "resolution": drawn by the app from the detected monitors, no fixed choices
    std::vector<SettingChoice> choices;  // toggle: [off, on]
};

struct SettingPreset {
    std::string id, name, description;
    std::map<std::string, std::string> values;
};

struct SettingKey {
    std::string file, section, key;
    std::optional<std::string> def;
};

// The launcher's settings model. The schema (rows, presets, key defaults) comes from
// config/settings.json; values are written to the game's ste_*.ini files.
class Settings {
public:
    bool loadSchema(const std::filesystem::path& json, std::string& err);

    // Values live in <dir>/ste_<file>.ini. Missing files mean "all defaults".
    void load(const std::filesystem::path& dir);
    void reload(const std::filesystem::path& dir);  // like load, but unreadable files keep the values in memory
    void clearAll() { values_.clear(); enforce(); }  // every key back to the game's default (for a one-off clean start)
    bool save(const std::filesystem::path& dir, std::string& err) const;

    const std::vector<SettingRow>& page(const std::string& name) const;
    const std::vector<SettingPreset>& presets() const { return presets_; }

    // Index of the choice the current values correspond to, or -1 ("Custom").
    int currentChoice(const SettingRow& row) const;
    void choose(const SettingRow& row, int index);
    void cycle(const SettingRow& row, int delta);  // wraps; from Custom starts at the first/last choice

    // Id of the preset the values equal, or "" for Custom.
    std::string activePreset() const;
    void applyPreset(const std::string& id);
    // A value every preset gets unless it names the key itself (e.g. variable refresh Off while the game
    // build has a known problem with Auto). Presets then still match "their" values, so no false "Custom".
    void setPresetDefault(const std::string& key, const std::optional<std::string>& v);
    bool hasPresetDefault(const std::string& key) const { return presetDefaults_.count(key) > 0; }
    const SettingPreset* preset(const std::string& id) const;
    std::map<std::string, std::string> presetEnv(const std::string& id) const;

    // Direct access by key id ("video.width"); nullopt removes the key (the game default applies).
    std::optional<std::string> value(const std::string& id) const { return effective(id); }
    bool hasValue(const std::string& id) const { return values_.count(id) > 0; }
    void set(const std::string& id, const std::optional<std::string>& v);

    // Name of the ini file for a file id ("video" -> "ste_video.ini").
    static std::string fileName(const std::string& fileId) { return "ste_" + fileId + ".ini"; }
    std::vector<std::string> fileIds() const;

private:
    std::optional<std::string> effective(const std::string& key) const;
    static bool sameValue(const std::optional<std::string>& a, const std::optional<std::string>& b);

    std::map<std::string, SettingKey> keys_;
    std::map<std::string, std::vector<SettingRow>> pages_;
    std::vector<SettingPreset> presets_;
    std::map<std::string, std::map<std::string, std::string>> presetEnv_;
    void enforce();  // forced values, applied after every load / preset
    std::map<std::string, std::string> values_;  // key id -> value; absent = game default
    std::map<std::string, std::string> presetDefaults_;
    std::set<std::string> presetIgnore_;        // keys presets neither set nor compare (screen choices)
    std::map<std::string, std::string> forced_;  // keys that always have this value
    std::map<std::string, std::map<std::string, std::string>> migrate_;  // key -> old value -> new value
};
