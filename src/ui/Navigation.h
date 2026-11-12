#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

#include "ui/NavCore.h"


// Turns keyboard and gamepad input into NavActions and moves RmlUi focus between
// elements marked class="nav". Mouse is handled by RmlUi itself. Independent of the
// window, so the same code can drive an in-game overlay.
class Navigation : public NavCore {
public:
    // Returns true when the event was consumed (do not forward it to RmlUi).
    bool handleEvent(const SDL_Event& ev);
    // Auto-repeat for held directions; call once per frame.
    void update(double dt);
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

private:
    void setHeld(NavAction dir);
    void updateHeldFromSources();

    std::vector<SDL_Gamepad*> pads_;
    SDL_Gamepad* activePad_ = nullptr;
    bool padsChanged_ = false;

    bool dpad_[4] = {false, false, false, false};  // up down left right
    NavAction stick_ = NavAction::None;
    float stickX_ = 0, stickY_ = 0;
    NavAction held_ = NavAction::None;
    double holdTime_ = 0, nextRepeat_ = 0;
};

