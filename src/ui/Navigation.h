#pragma once
#include <RmlUi/Core/Types.h>
#include <SDL3/SDL.h>

#include <functional>
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

// Turns keyboard and gamepad input into NavActions and moves RmlUi focus between
// elements marked class="nav". Mouse is handled by RmlUi itself. Independent of the
// window, so the same code can drive an in-game overlay.
class Navigation {
public:
    bool swapConfirmBack = false;  // Nintendo layout
    // "auto" = match the connected pad; "xbox" | "xbox_original" | "playstation" force one set of button names.
    std::string promptStyle = "auto";

    // Returns true when the event was consumed (do not forward it to RmlUi).
    bool handleEvent(const SDL_Event& ev);
    // Auto-repeat for held directions; call once per frame.
    void update(double dt);
    // Actions produced since the last call, in order.
    std::vector<NavAction> take();
    void push(NavAction a) { pending_.push_back(a); }

    // Moves focus / activates the focused element for Up/Down/Left/Right/Confirm.
    // Left/Right on an element with data-adjust are left to the app (it changes the value).
    // Returns true if the action was handled here, false if the app must handle it.
    bool perform(Rml::Context* ctx, Rml::ElementDocument* doc, NavAction a);

    struct PadInfo {
        SDL_JoystickID id = 0;
        std::string name, guid;
        DeviceKind kind = DeviceKind::Generic;
    };
    // Connected gamepads in connection order.
    std::vector<PadInfo> padList() const;
    // Short vibration on one pad (strength 0..1); false if the pad cannot rumble.
    bool rumble(SDL_JoystickID id, float strength, unsigned ms);
    // True once after a pad was plugged in or removed.
    bool takePadsChanged();

    // While set, every event goes here first; returning true consumes it (used to listen for a button to bind).
    std::function<bool(const SDL_Event&)> capture;

    DeviceKind device() const { return device_; }
    void forceDevice(DeviceKind k) { device_ = k; deviceChanged_ = true; deviceLocked_ = true; }  // tests: stop following the real input device
    HintLabels hints() const;
    // True once after the device kind (and so the hint bar) changed.
    bool takeDeviceChanged();

    static bool parseAction(const std::string& name, NavAction& out);

private:
    void setDevice(DeviceKind k);
    void setHeld(NavAction dir);
    void updateHeldFromSources();
    std::string buttonLabel(SDL_GamepadButton b) const;

    std::vector<NavAction> pending_;
    std::vector<SDL_Gamepad*> pads_;
    SDL_Gamepad* activePad_ = nullptr;
    DeviceKind device_ = DeviceKind::Keyboard;
    bool deviceLocked_ = false;
    bool deviceChanged_ = true;
    bool padsChanged_ = false;

    bool dpad_[4] = {false, false, false, false};  // up down left right
    NavAction stick_ = NavAction::None;
    float stickX_ = 0, stickY_ = 0;
    NavAction held_ = NavAction::None;
    double holdTime_ = 0, nextRepeat_ = 0;
};

