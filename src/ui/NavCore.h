#pragma once
// The window-free part of navigation: actions, the input device kind, button hints and focus movement in an RmlUi document.
// No SDL: the launcher's `Navigation` (SDL events) and the in-game overlay (neutral input from the host) both build on it.
#include <string>
#include <vector>

namespace Rml { class Context; class ElementDocument; }

enum class NavAction { None, Up, Down, Left, Right, Confirm, Back, PrevTab, NextTab, Play, Extra1, Extra2 };
enum class DeviceKind { Keyboard, Xbox, PlayStation, Switch, Generic };

struct HintLabels {
    std::string confirm, back, tabs, start, x, y;
    // Button pictures (glyph file stems, assets/glyphs) for the same hints; empty = show the text above as a key cap.
    // `tabs` has two (previous, next tab). Keyboard hints never have pictures.
    std::vector<std::string> confirmG, backG, tabsG, startG, xG, yG;
};

class NavCore {
public:
    bool swapConfirmBack = false;  // Nintendo layout
    // "auto" = match the input device; "xbox" | "xbox_original" | "playstation" force one set of button names.
    std::string promptStyle = "auto";

    // Actions produced since the last call, in order.
    std::vector<NavAction> take();
    void push(NavAction a) { pending_.push_back(a); }

    // Moves focus / activates the focused element for Up/Down/Left/Right/Confirm.
    // Left/Right on an element with data-adjust are left to the app (it changes the value).
    // Returns true if the action was handled here, false if the app must handle it.
    bool perform(Rml::Context* ctx, Rml::ElementDocument* doc, NavAction a);

    DeviceKind device() const { return device_; }
    void forceDevice(DeviceKind k) { device_ = k; deviceChanged_ = true; deviceLocked_ = true; }  // stop following the real input device
    HintLabels hints() const;
    // True once after the device kind (and so the hint bar) changed.
    bool takeDeviceChanged();

    static bool parseAction(const std::string& name, NavAction& out);

protected:
    void setDevice(DeviceKind k);

    std::vector<NavAction> pending_;
    DeviceKind device_ = DeviceKind::Keyboard;
    bool deviceLocked_ = false;
    bool deviceChanged_ = true;
};
