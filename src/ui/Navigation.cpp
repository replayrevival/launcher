#include "ui/Navigation.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kFirstRepeat = 0.40, kRepeat = 0.12;
constexpr float kStickThreshold = 0.55f;

DeviceKind kindFromType(SDL_GamepadType t) {
    switch (t) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5: return DeviceKind::PlayStation;
        case SDL_GAMEPAD_TYPE_XBOX360:
        case SDL_GAMEPAD_TYPE_XBOXONE: return DeviceKind::Xbox;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return DeviceKind::Switch;
        default: return DeviceKind::Generic;
    }
}

int dirIndex(NavAction a) {
    switch (a) {
        case NavAction::Up: return 0;
        case NavAction::Down: return 1;
        case NavAction::Left: return 2;
        case NavAction::Right: return 3;
        default: return -1;
    }
}

bool isFocusable(Rml::Element* e) {
    return e && e->IsVisible(true) && !e->HasAttribute("disabled");
}

}  // namespace

bool Navigation::parseAction(const std::string& n, NavAction& out) {
    static const struct { const char* name; NavAction a; } table[] = {
        {"up", NavAction::Up},       {"down", NavAction::Down},   {"left", NavAction::Left},
        {"right", NavAction::Right}, {"confirm", NavAction::Confirm}, {"back", NavAction::Back},
        {"prev", NavAction::PrevTab}, {"next", NavAction::NextTab}, {"play", NavAction::Play},
        {"x", NavAction::Extra1}, {"y", NavAction::Extra2}};
    for (auto& t : table)
        if (n == t.name) { out = t.a; return true; }
    return false;
}

void Navigation::setDevice(DeviceKind k) {
    if (k != device_) { device_ = k; deviceChanged_ = true; }
}

bool Navigation::takeDeviceChanged() {
    bool c = deviceChanged_;
    deviceChanged_ = false;
    return c;
}

std::vector<NavAction> Navigation::take() {
    std::vector<NavAction> out;
    out.swap(pending_);
    return out;
}

std::string Navigation::buttonLabel(SDL_GamepadButton b) const {
    if (!activePad_) return "";
    switch (SDL_GetGamepadButtonLabel(activePad_, b)) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
        default: return "?";
    }
}

HintLabels Navigation::hints() const {
    if (device_ == DeviceKind::Keyboard) return {"Enter", "Esc", "Q E", "P", "X", "Y"};
    const SDL_GamepadButton c = swapConfirmBack ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
    const SDL_GamepadButton b = swapConfirmBack ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
    HintLabels h{buttonLabel(c), buttonLabel(b), "LB RB", "START", buttonLabel(SDL_GAMEPAD_BUTTON_WEST), buttonLabel(SDL_GAMEPAD_BUTTON_NORTH)};
    if (device_ == DeviceKind::PlayStation) { h.tabs = "L1 R1"; h.start = "OPTIONS"; }
    if (device_ == DeviceKind::Switch) { h.tabs = "L R"; h.start = "+"; }
    if (promptStyle == "xbox" || promptStyle == "xbox_original") {
        h.confirm = swapConfirmBack ? "B" : "A";
        h.back = swapConfirmBack ? "A" : "B";
        h.x = "X"; h.y = "Y"; h.start = "START";
        h.tabs = promptStyle == "xbox" ? "LB RB" : "BLACK WHITE";
    } else if (promptStyle == "playstation") {
        h.confirm = swapConfirmBack ? "Circle" : "Cross";
        h.back = swapConfirmBack ? "Cross" : "Circle";
        h.x = "Square"; h.y = "Triangle"; h.start = "OPTIONS"; h.tabs = "L1 R1";
    }
    return h;
}

void Navigation::setHeld(NavAction dir) {
    if (dir == held_) return;
    held_ = dir;
    holdTime_ = 0;
    nextRepeat_ = kFirstRepeat;
    if (dir != NavAction::None) pending_.push_back(dir);
}

void Navigation::updateHeldFromSources() {
    if (stick_ != NavAction::None) return setHeld(stick_);
    static const NavAction order[4] = {NavAction::Up, NavAction::Down, NavAction::Left, NavAction::Right};
    for (int i = 0; i < 4; ++i)
        if (dpad_[i]) return setHeld(order[i]);
    setHeld(NavAction::None);
}

void Navigation::update(double dt) {
    if (held_ == NavAction::None) return;
    holdTime_ += dt;
    while (holdTime_ >= nextRepeat_) {
        pending_.push_back(held_);
        nextRepeat_ += kRepeat;
    }
}

std::vector<Navigation::PadInfo> Navigation::padList() const {
    std::vector<PadInfo> out;
    for (SDL_Gamepad* g : pads_) {
        PadInfo p;
        p.id = SDL_GetGamepadID(g);
        const char* n = SDL_GetGamepadName(g);
        p.name = n ? n : "Controller";
        char buf[64] = {};
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(p.id), buf, sizeof buf);
        p.guid = buf;
        p.kind = kindFromType(SDL_GetGamepadType(g));
        out.push_back(std::move(p));
    }
    return out;
}

bool Navigation::rumble(SDL_JoystickID id, float strength, unsigned ms) {
    for (SDL_Gamepad* g : pads_)
        if (SDL_GetGamepadID(g) == id) {
            const Uint16 v = Uint16(std::fmin(std::fmax(strength, 0.f), 1.f) * 65535.f);
            return SDL_RumbleGamepad(g, v, v, ms);
        }
    return false;
}

bool Navigation::takePadsChanged() {
    bool c = padsChanged_;
    padsChanged_ = false;
    return c;
}

bool Navigation::handleEvent(const SDL_Event& ev) {
    if (capture && capture(ev)) return true;
    switch (ev.type) {
        case SDL_EVENT_GAMEPAD_ADDED: {
            padsChanged_ = true;
            if (SDL_Gamepad* g = SDL_OpenGamepad(ev.gdevice.which)) {
                if (std::find(pads_.begin(), pads_.end(), g) != pads_.end()) return true;  // already listed (SDL hands back the open pad)
                pads_.push_back(g);
                SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(g));
                if (!activePad_) activePad_ = g;
            }
            return true;
        }
        case SDL_EVENT_GAMEPAD_REMOVED: {
            padsChanged_ = true;
            for (size_t i = 0; i < pads_.size(); ++i) {
                if (SDL_GetGamepadID(pads_[i]) == ev.gdevice.which) {
                    if (activePad_ == pads_[i]) activePad_ = nullptr;
                    SDL_CloseGamepad(pads_[i]);
                    pads_.erase(pads_.begin() + i);
                    break;
                }
            }
            if (!activePad_ && !pads_.empty()) activePad_ = pads_[0];
            if (!activePad_) setDevice(DeviceKind::Keyboard);
            dpad_[0] = dpad_[1] = dpad_[2] = dpad_[3] = false;
            stick_ = NavAction::None;
            updateHeldFromSources();
            return true;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const bool down = ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            for (SDL_Gamepad* g : pads_)
                if (SDL_GetGamepadID(g) == ev.gbutton.which) activePad_ = g;
            if (activePad_) setDevice(kindFromType(SDL_GetGamepadType(activePad_)));
            const auto b = SDL_GamepadButton(ev.gbutton.button);
            if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) dpad_[0] = down;
            else if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) dpad_[1] = down;
            else if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) dpad_[2] = down;
            else if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) dpad_[3] = down;
            else if (down) {
                const SDL_GamepadButton confirm = swapConfirmBack ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
                const SDL_GamepadButton back = swapConfirmBack ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
                if (b == confirm) pending_.push_back(NavAction::Confirm);
                else if (b == back) pending_.push_back(NavAction::Back);
                else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) pending_.push_back(NavAction::PrevTab);
                else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) pending_.push_back(NavAction::NextTab);
                else if (b == SDL_GAMEPAD_BUTTON_START) pending_.push_back(NavAction::Play);
                else if (b == SDL_GAMEPAD_BUTTON_WEST) pending_.push_back(NavAction::Extra1);
                else if (b == SDL_GAMEPAD_BUTTON_NORTH) pending_.push_back(NavAction::Extra2);
            }
            updateHeldFromSources();
            return true;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const auto axis = SDL_GamepadAxis(ev.gaxis.axis);
            if (axis != SDL_GAMEPAD_AXIS_LEFTX && axis != SDL_GAMEPAD_AXIS_LEFTY) return true;
            float& x = stickX_;
            float& y = stickY_;
            const float v = ev.gaxis.value / 32767.f;
            (axis == SDL_GAMEPAD_AXIS_LEFTX ? x : y) = v;
            NavAction d = NavAction::None;
            if (std::fabs(x) >= kStickThreshold || std::fabs(y) >= kStickThreshold)
                d = std::fabs(x) > std::fabs(y) ? (x < 0 ? NavAction::Left : NavAction::Right)
                                                : (y < 0 ? NavAction::Up : NavAction::Down);
            if (d != NavAction::None) {
                for (SDL_Gamepad* g : pads_)
                    if (SDL_GetGamepadID(g) == ev.gaxis.which) activePad_ = g;
                if (activePad_) setDevice(kindFromType(SDL_GetGamepadType(activePad_)));
            }
            stick_ = d;
            updateHeldFromSources();
            return true;
        }
        case SDL_EVENT_KEY_DOWN: {
            NavAction a = NavAction::None;
            switch (ev.key.key) {
                case SDLK_UP: a = NavAction::Up; break;
                case SDLK_DOWN: a = NavAction::Down; break;
                case SDLK_LEFT: a = NavAction::Left; break;
                case SDLK_RIGHT: a = NavAction::Right; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: a = NavAction::Confirm; break;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE: a = NavAction::Back; break;
                case SDLK_Q:
                case SDLK_PAGEUP: a = NavAction::PrevTab; break;
                case SDLK_E:
                case SDLK_PAGEDOWN: a = NavAction::NextTab; break;
                case SDLK_X: a = NavAction::Extra1; break;
                case SDLK_Y: a = NavAction::Extra2; break;
                case SDLK_P:
                case SDLK_F5: a = NavAction::Play; break;
                default: return false;
            }
            if (ev.key.repeat && dirIndex(a) < 0) return true;
            setDevice(DeviceKind::Keyboard);
            pending_.push_back(a);
            return true;
        }
        default: return false;
    }
}

bool Navigation::perform(Rml::Context* ctx, Rml::ElementDocument* doc, NavAction a) {
    if (dirIndex(a) < 0 && a != NavAction::Confirm) return false;
    if ((a == NavAction::Left || a == NavAction::Right) && ctx->GetFocusElement() && ctx->GetFocusElement()->HasAttribute("data-adjust"))
        return false;

    // An open message box keeps focus to itself.
    Rml::ElementList all;
    doc->QuerySelectorAll(all, ".modal .nav");
    if (all.empty()) doc->QuerySelectorAll(all, ".nav");
    std::vector<Rml::Element*> items;
    for (auto* e : all)
        if (isFocusable(e)) items.push_back(e);
    if (items.empty()) return true;

    Rml::Element* cur = ctx->GetFocusElement();
    bool curIsItem = false;
    for (auto* e : items) curIsItem |= (e == cur);
    if (!curIsItem) {
        // Nothing sensible focused yet: take the preferred element, else the first.
        Rml::Element* pick = doc->QuerySelector(".nav.default");
        items.front()->Focus();
        if (isFocusable(pick)) pick->Focus();
        return true;
    }

    if (a == NavAction::Confirm) {
        cur->Click();
        return true;
    }

    const Rml::Vector2f c = cur->GetAbsoluteOffset(Rml::BoxArea::Border) + cur->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
    Rml::Vector2f dir{0, 0};
    if (a == NavAction::Up) dir = {0, -1};
    else if (a == NavAction::Down) dir = {0, 1};
    else if (a == NavAction::Left) dir = {-1, 0};
    else dir = {1, 0};

    Rml::Element* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (auto* e : items) {
        if (e == cur) continue;
        const Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border) + e->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
        const Rml::Vector2f d = p - c;
        const float along = d.x * dir.x + d.y * dir.y;
        if (along <= 1.f) continue;
        const float across = std::fabs(d.x * dir.y) + std::fabs(d.y * dir.x);
        const float score = along + 2.5f * across;
        if (score < bestScore) { bestScore = score; best = e; }
    }
    // "Nearest": scroll only if the row is out of view, and by the least amount (no jumping).
    if (best) { best->Focus(); best->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest)); }
    return true;
}

