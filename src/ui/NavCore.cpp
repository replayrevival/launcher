#include "ui/NavCore.h"

#include <RmlUi/Core.h>

#include <cmath>
#include <limits>

#include "core/input_map.h"

namespace {

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

bool NavCore::parseAction(const std::string& n, NavAction& out) {
    static const struct { const char* name; NavAction a; } table[] = {
        {"up", NavAction::Up},       {"down", NavAction::Down},   {"left", NavAction::Left},
        {"right", NavAction::Right}, {"confirm", NavAction::Confirm}, {"back", NavAction::Back},
        {"prev", NavAction::PrevTab}, {"next", NavAction::NextTab}, {"play", NavAction::Play},
        {"x", NavAction::Extra1}, {"y", NavAction::Extra2}};
    for (auto& t : table)
        if (n == t.name) { out = t.a; return true; }
    return false;
}

void NavCore::setDevice(DeviceKind k) {
    if (!deviceLocked_ && k != device_) { device_ = k; deviceChanged_ = true; }
}

bool NavCore::takeDeviceChanged() {
    bool c = deviceChanged_;
    deviceChanged_ = false;
    return c;
}

std::vector<NavAction> NavCore::take() {
    std::vector<NavAction> out;
    out.swap(pending_);
    return out;
}

HintLabels NavCore::hints() const {
    if (device_ == DeviceKind::Keyboard) return {"Enter", "Esc", "Q E", "P", "X", "Y"};
    // Button names by pad family (south / east / west / north face button).
    const char* face[4] = {"A", "B", "X", "Y"};  // south east west north on Xbox-like pads: A B X Y
    if (device_ == DeviceKind::PlayStation) { face[0] = "Cross"; face[1] = "Circle"; face[2] = "Square"; face[3] = "Triangle"; }
    if (device_ == DeviceKind::Switch) { face[0] = "B"; face[1] = "A"; face[2] = "Y"; face[3] = "X"; }
    HintLabels h{swapConfirmBack ? face[1] : face[0], swapConfirmBack ? face[0] : face[1], "LB RB", "START", face[2], face[3]};
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
    // Pictures: forced styles, or Auto with an Xbox / PlayStation pad. Switch and unknown pads keep the text key caps.
    const PromptStyle ps = parsePromptStyle(promptStyle);
    PadFamily fam = PadFamily::Generic;
    if (ps == PromptStyle::Auto) {
        if (device_ == DeviceKind::PlayStation) fam = PadFamily::PlayStation;
        else if (device_ == DeviceKind::Xbox) fam = PadFamily::Xbox;
    }
    auto pic = [&](const char* src) {
        const std::string g = glyphFile(src, fam, ps);
        return g.empty() ? std::vector<std::string>{} : std::vector<std::string>{g};
    };
    auto both = [&](const char* a, const char* b) {
        const std::string ga = glyphFile(a, fam, ps), gb = glyphFile(b, fam, ps);
        return ga.empty() || gb.empty() ? std::vector<std::string>{} : std::vector<std::string>{ga, gb};
    };
    if (ps != PromptStyle::Auto || fam != PadFamily::Generic) {
        h.confirmG = pic(swapConfirmBack ? "b" : "a");
        h.backG = pic(swapConfirmBack ? "a" : "b");
        h.xG = pic("x"); h.yG = pic("y"); h.startG = pic("start");
        h.tabsG = both("leftshoulder", "rightshoulder");
    }
    return h;
}

bool NavCore::perform(Rml::Context* ctx, Rml::ElementDocument* doc, NavAction a) {
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

