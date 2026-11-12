#pragma once
#include <array>
#include <map>
#include <string>
#include <vector>

// Controller mapping: which device sits on each of the game's 4 ports and how its controls
// are bound. Pure data (no SDL), saved as input_mapping.json; format in docs/INPUT_MAPPING.md.

struct ControlAction {
    std::string id, label, kind;  // kind: button | trigger | stick
    std::string def;              // default pad source (SDL name, e.g. "leftshoulder")
    std::string keyDef;           // default keyboard binding (SDL key name)
};

// One device profile: remembered per device GUID ("keyboard" for the keyboard,
// "default" for a port with no pad connected).
struct PadSettings {
    std::string name;                           // last seen device name, for people reading the file
    std::map<std::string, std::string> bindings;  // action id -> source ("a", "lefttrigger", "key:Z")
    float deadzone = 0.15f;
    int rumble = 100;                           // 0 (off) .. 100 %
};

// What a port resolved to at save time (the launcher knows which pads are plugged in).
struct ResolvedPort {
    std::string guid, name;  // empty when nothing is attached
};

enum class PadFamily { Xbox, PlayStation, Switch, Generic };

// "Button prompts" setting ([input] button_prompts in the game's ini): which button names to show.
enum class PromptStyle { Auto, Xbox, XboxOriginal, PlayStation };
PromptStyle parsePromptStyle(const std::string& value);  // unknown = Auto

class InputMap {
public:
    static constexpr int kPorts = 4;

    bool loadSchema(const std::string& controlsJson, std::string& err);
    bool load(const std::string& file);  // missing/damaged file = defaults
    bool save(const std::string& file, const std::array<ResolvedPort, kPorts>& resolved, std::string& err) const;

    const std::vector<ControlAction>& actions() const { return actions_; }
    const ControlAction* action(const std::string& id) const;
    float deadzoneMin() const { return dzMin_; }
    float deadzoneMax() const { return dzMax_; }
    float deadzoneStep() const { return dzStep_; }

    // Port device: "auto" (Nth connected pad), "keyboard", "none", or a device GUID.
    std::string device[kPorts] = {"auto", "auto", "auto", "auto"};
    bool swapConfirmBack = false;

    // Which profile edits on this port go to. Empty for "none".
    static std::string profileIdFor(const std::string& device, const std::string& resolvedGuid);
    PadSettings& profile(const std::string& id);  // created with defaults on first use
    void resetBindings(const std::string& id);

    // Bind `source` to `actionId`. If another action already used it, the two swap.
    // Returns false if the source is not allowed for that action.
    bool assign(const std::string& id, const std::string& actionId, const std::string& source);

    static bool isKeySource(const std::string& s) { return s.rfind("key:", 0) == 0; }
    // `style` other than Auto overrides the pad's own family (Original Xbox also calls the shoulders Black / White).
    static std::string label(const std::string& source, PadFamily f, PromptStyle style = PromptStyle::Auto);
    static std::string defaultName(const std::string& profileId);

private:
    PadSettings makeDefault(const std::string& id) const;

    std::vector<ControlAction> actions_;
    float dzMin_ = 0, dzMax_ = 0.5f, dzStep_ = 0.05f, dzDefault_ = 0.15f;
    std::map<std::string, PadSettings> profiles_;
};
