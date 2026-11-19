#include <cstdio>
#include "ui/Overlay.h"

#include <algorithm>
#include <chrono>
#include <set>

#include "core/identify.h"
#include "ui/views/rows.h"

namespace {
constexpr const char* kCtx = "overlay";

std::string navButton(const char* label, const char* action, const char* arg, const char* key, bool def = false) {
    return std::string("<button class=\"nav") + (def ? " default" : "") + "\" data-action=\"" + action + "\" data-arg=\"" + arg +
           "\" data-key=\"" + key + "\"><span class=\"marker\"/>" + label + "</button>";
}
}  // namespace

bool Overlay::init(LauncherModel& model, const OverlayInit& init, OverlayCallbacks cb, std::string& err) {
    shutdown();
    model_ = &model;
    ctl_.attach(model);
    init_ = init;
    cb_ = std::move(cb);
    views::setGlyphDir(pathToUtf8(init.resourceDir / "assets" / "glyphs"));
    ctx_ = Rml::CreateContext(kCtx, Rml::Vector2i(init.width, init.height));
    if (!ctx_) { err = "Could not create the overlay screen"; return false; }
    resize(init.width, init.height, init.contentScale);
    doc_ = ctx_->LoadDocument(pathToUtf8(init.resourceDir / "ui" / "rml" / "overlay.rml"));
    if (!doc_) {
        err = "Could not load the overlay screen";
        Rml::RemoveContext(kCtx);
        ctx_ = nullptr;
        return false;
    }
    doc_->AddEventListener(Rml::EventId::Click, this);
    doc_->AddEventListener(Rml::EventId::Mouseover, this);
    doc_->Show();
    screen_ = Screen::Main;
    return true;
}

void Overlay::shutdown() {
    if (ctx_) Rml::RemoveContext(kCtx);
    ctx_ = nullptr;
    doc_ = nullptr;
    visible_ = false;
    held_ = NavAction::None;
}

void Overlay::resize(int w, int h, float contentScale) {
    init_.width = w;
    init_.height = h;
    init_.contentScale = contentScale;
    if (!ctx_) return;
    ctx_->SetDimensions(Rml::Vector2i(w, h));
    ctx_->SetDensityIndependentPixelRatio(float(h) / 820.f * contentScale);
}

void Overlay::show() {
    if (!ctx_ || visible_) return;
    visible_ = true;
    go(Screen::Main);
    if (cb_.onShow) cb_.onShow();
}

void Overlay::hide() {
    if (!visible_) return;
    ctl_.endListen();
    visible_ = false;
    held_ = NavAction::None;
    if (cb_.onHide) cb_.onHide();
}

bool Overlay::feedNav(NavAction a) {
    if (!visible_ || !ctx_) return false;
    if (listening()) return true;  // the next press is a binding, not navigation (the host sends it with feedSource)
    nav_.push(a);
    held_ = a;
    nextRepeatMs_ = nowMs_ + 400.0;
    return true;
}

bool Overlay::feedNavRelease(NavAction a) {
    repeatOn_ = true;
    if (held_ == a) held_ = NavAction::None;
    return visible_;
}

bool Overlay::feedSource(const std::string& source) {
    if (!visible_ || !ctx_) return false;
    if (!listening()) return false;
    if (source == "guide") return true;  // reserved for opening the overlay
    std::string err;
    if (source != "cancel") {
        ctl_.applyCapture(source, err);
        saveInputNote(err);
    } else {
        ctl_.endListen();
    }
    renderScreen();
    return true;
}

void Overlay::setPads(std::vector<PadDesc> pads) {
    ctl_.setPads(std::move(pads));
    if (visible_ && screen_ == Screen::Controls) renderScreen();
}

// After input_mapping.json was written: tell the game it can re-read it, or show why it could not be saved.
void Overlay::saveInputNote(const std::string& err) {
    if (!err.empty()) { note_ = "Could not save controller settings: " + err; return; }
    if (cb_.applySetting) cb_.applySetting("input.mapping", pathToUtf8(model_->inputMapPath()));
}

bool Overlay::feedMouseMove(int x, int y) {
    if (!visible_ || !ctx_) return false;
    ctx_->ProcessMouseMove(x, y, 0);
    return true;
}

bool Overlay::feedMouseButton(int button, bool down) {
    if (!visible_ || !ctx_) return false;
    if (down) ctx_->ProcessMouseButtonDown(button, 0);
    else ctx_->ProcessMouseButtonUp(button, 0);
    return true;
}

bool Overlay::feedMouseWheel(float up) {
    if (!visible_ || !ctx_) return false;
    ctx_->ProcessMouseWheel(Rml::Vector2f(0, -up), 0);
    return true;
}

void Overlay::update(double dt) {
    if (!visible_ || !ctx_) return;
    nowMs_ += dt * 1000.0;
    if (repeatOn_ && (held_ == NavAction::Up || held_ == NavAction::Down || held_ == NavAction::Left || held_ == NavAction::Right)) {
        while (nowMs_ >= nextRepeatMs_) { nav_.push(held_); nextRepeatMs_ += 90.0; }
    }
    if (listening() && uint64_t(nowMs_) - ctl_.listenSince > ControllersUi::kListenTimeoutMs) {
        ctl_.endListen();
        renderScreen();
    }
    for (NavAction a : nav_.take()) {
        if (!visible_) break;
        if (nav_.perform(ctx_, doc_, a)) continue;
        handleNav(a);
    }
    if (visible_ && nav_.takeDeviceChanged()) renderHints();
    ctx_->Update();
}

void Overlay::render() {
    if (visible_ && ctx_) ctx_->Render();
}

void Overlay::handleNav(NavAction a) {
    Rml::Element* f = ctx_->GetFocusElement();
    if ((a == NavAction::Left || a == NavAction::Right) && f && f->HasAttribute("data-adjust")) {
        const std::string kind = f->GetAttribute<Rml::String>("data-adjust", ""), id = f->GetAttribute<Rml::String>("data-arg", "");
        const int dir = a == NavAction::Right ? +1 : -1;
        if (kind.rfind("ctl-", 0) == 0) {
            std::string err;
            if (ctl_.adjust(kind, id, dir, err)) saveInputNote(err);
            renderScreen();
        } else {
            step(id, dir);
        }
        return;
    }
    if ((a == NavAction::PrevTab || a == NavAction::NextTab) && !listening()) {  // LB / RB: Video <-> Enhancements <-> Controllers
        static const Screen tabs[] = {Screen::Video, Screen::Enhance, Screen::Controls};
        int i = -1;
        for (int k = 0; k < 3; ++k) if (tabs[k] == screen_) i = k;
        const int dir = a == NavAction::NextTab ? +1 : -1;
        if (i < 0) i = dir > 0 ? -1 : 3;  // from the main menu or a confirm page: first / last tab
        const int n = i + dir;
        if (n >= 0 && n < 3) go(tabs[n]);  // no wrap: the ends stay put
        return;
    }
    if (a == NavAction::Back) {
        if (listening()) { ctl_.endListen(); renderScreen(); return; }
        if (screen_ == Screen::Main) hide();
        else go(Screen::Main);
    }
}

void Overlay::ProcessEvent(Rml::Event& ev) {
    Rml::Element* e = ev.GetTargetElement();
    while (e && !e->HasAttribute("data-action")) e = e->GetParentNode();
    if (!e) return;
    if (ev.GetId() == Rml::EventId::Mouseover) {
        while (e && !e->IsClassSet("nav")) e = e->GetParentNode();
        if (e) e->Focus();
        return;
    }
    act(e->GetAttribute<Rml::String>("data-action", ""), e->GetAttribute<Rml::String>("data-arg", ""));
}

void Overlay::act(const std::string& action, const std::string& arg) {
    if (action == "ov-resume") {
        hide();
    } else if (action == "ov-go") {
        go(arg == "video" ? Screen::Video
           : arg == "enhance" ? Screen::Enhance
           : arg == "controls" ? Screen::Controls
           : arg == "launcher" ? Screen::ConfirmLauncher
           : arg == "quit" ? Screen::ConfirmQuit
                           : Screen::Main);
    } else if (action == "ov-yes") {
        const bool toLauncher = screen_ == Screen::ConfirmLauncher;
        hide();
        if (toLauncher) { if (cb_.returnToLauncher) cb_.returnToLauncher(); }
        else if (cb_.quitGame) cb_.quitGame();
    } else if (action.rfind("ctl-", 0) == 0) {
        controllerAction(action, arg);
    } else if (action == "step") {  // chevrons: "<row id>@<-1|1>"
        const size_t at = arg.rfind('@');
        if (at != std::string::npos) step(arg.substr(0, at), std::atoi(arg.c_str() + at + 1) < 0 ? -1 : +1);
    } else if (action == "cycle") {  // click on the value
        step(arg, +1);
    }
}

void Overlay::controllerAction(const std::string& action, const std::string& arg) {
    std::string err;
    if (action != "ctl-bind" && listening()) ctl_.endListen();
    if (action == "ctl-step") {
        if (ctl_.adjustFromStep(arg, err)) saveInputNote(err);
    } else if (action == "ctl-port") {
        ctl_.selectPort(std::atoi(arg.c_str()));
    } else if (action == "ctl-bind") {
        if (ctl_.listenAction == arg) ctl_.endListen();
        else ctl_.beginListen(arg, uint64_t(nowMs_));
    } else if (action == "ctl-reset") {
        ctl_.resetBindings(err);
        saveInputNote(err);
    } else if (action == "ctl-swap") {
        ctl_.toggleSwapConfirmBack(err);
        nav_.swapConfirmBack = model_->input.swapConfirmBack;
        saveInputNote(err);
    } else if (action == "ctl-test") {
        const auto info = ctl_.resolvePorts();
        const std::string pid = ctl_.profileIdOf(info, ctl_.port);
        const auto& pi = info[size_t(ctl_.port)];
        if (pi.padId == 0) note_ = "No controller on this port. Connect one, or choose the controller for this port.";
        else if (pid.empty() || model_->input.profile(pid).rumble == 0) note_ = "Rumble is off for this controller. Turn it on first.";
        else if (!cb_.rumble || !cb_.rumble(pi.padId, model_->input.profile(pid).rumble / 100.f, 700)) note_ = "This controller cannot rumble.";
        else note_.clear();
        renderScreen();
        return;
    } else if (ctl_.adjust(action, arg, +1, err)) {  // ctl-device / ctl-deadzone / ctl-rumble / ctl-stick by click
        saveInputNote(err);
    }
    renderScreen();
}

// One row stepped (chevron, click on the value, or Left/Right): the model changes and saves it, the game is told.
void Overlay::step(const std::string& id, int dir) {
    const auto r = model_->stepSetting(id, dir, uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
    note_ = r.saveError.empty() ? "" : "Could not save: " + r.saveError;
    notifyGame(id);
    renderScreen();
}

void Overlay::notifyGame(const std::string& rowId) {
    if (!cb_.applySetting) return;
    for (const char* page : {"video", "enhance", "controls"})
        for (auto& row : model_->settings.page(page)) {
            if (row.id != rowId) continue;
            std::set<std::string> keys;
            for (auto& c : row.choices)
                for (auto& kv : c.set) keys.insert(kv.first);
            bool refused = false;
            int worst = 0;  // 0 now, 1 next match, 2 after restart
            for (auto& k : keys) {
                const auto v = model_->settings.value(k);
                const Applied a = cb_.applySetting(k, v ? *v : "");
                if (a == Applied::Refused) refused = true;
                worst = std::max(worst, a == Applied::Now ? 0 : a == Applied::NextMatch ? 1 : 2);
            }
            applied_[rowId] = worst == 0 ? "now" : worst == 1 ? "match" : "restart";
            if (refused && note_.empty())
                note_ = "The game could not apply that right now. It is saved and used the next time you start the game.";
            return;
        }
}

std::string Overlay::focusKey() const {
    Rml::Element* f = ctx_ ? ctx_->GetFocusElement() : nullptr;
    return f ? f->GetAttribute<Rml::String>("data-key", "") : "";
}

void Overlay::restoreFocus(const std::string& key) {
    Rml::Element* e = key.empty() ? nullptr : doc_->QuerySelector("[data-key=\"" + key + "\"]");
    if (!e) e = doc_->QuerySelector(".nav.default");
    if (!e) e = doc_->QuerySelector(".nav");
    if (e) e->Focus();
}

void Overlay::go(Screen s) {
    screen_ = s;
    {   // one log line per page change: the smoke tests (a pad-only run) read it
        static const char* names[] = {"main", "video", "enhance", "controls", "confirm-launcher", "confirm-quit"};
        std::fprintf(stderr, "[OVERLAY] page %s\n", names[int(s)]);
        std::fflush(stderr);
    }
    note_.clear();
    renderScreen();
    restoreFocus("");
}

void Overlay::renderScreen() {
    if (!doc_) return;
    const std::string key = focusKey();
    float scroll = 0;
    if (auto* p = doc_->GetElementById("ov-page")) scroll = p->GetScrollTop();
    std::string title, h;
    switch (screen_) {
        case Screen::Main:
            title = "PAUSED";
            h = "<div class=\"ov-menu\">" + navButton("RESUME", "ov-resume", "", "ov-resume", true) + navButton("VIDEO", "ov-go", "video", "ov-video") +
                navButton("ENHANCEMENTS", "ov-go", "enhance", "ov-enhance") + navButton("CONTROLLERS", "ov-go", "controls", "ov-controls") +
                navButton("RETURN TO LAUNCHER", "ov-go", "launcher", "ov-launcher") + navButton("QUIT GAME", "ov-go", "quit", "ov-quit") + "</div>";
            break;
        case Screen::Video: title = "VIDEO"; h = views::settingsRows(*model_, "video", true, &applied_); break;
        case Screen::Enhance: title = "ENHANCEMENTS"; h = views::settingsRows(*model_, "enhance", true, &applied_); break;
        case Screen::Controls:
            title = "CONTROLLERS";
            h = views::controllersPage(*model_, ctl_.viewState());
            break;
        case Screen::ConfirmLauncher:
        case Screen::ConfirmQuit:
            title = screen_ == Screen::ConfirmLauncher ? "RETURN TO LAUNCHER" : "QUIT GAME";
            h = "<p class=\"ov-text\">Leave the game? Unsaved progress is lost.</p><div class=\"ov-menu\">" +
                navButton("NO, KEEP PLAYING", "ov-go", "main", "ov-no", true) + navButton("YES, LEAVE", "ov-yes", "", "ov-yes") + "</div>";
            break;
    }
    if (auto* t = doc_->GetElementById("ov-title")) {
        const bool tabbed = screen_ == Screen::Video || screen_ == Screen::Enhance || screen_ == Screen::Controls;
        std::string html = views::esc(title);
        if (tabbed) {  // the three pages as a visible strip; LB / RB moves along it
            const struct { Screen s; const char* name; } t3[] = {{Screen::Video, "VIDEO"}, {Screen::Enhance, "ENHANCEMENTS"}, {Screen::Controls, "CONTROLLERS"}};
            html = "";
            for (auto& x : t3) html += std::string("<span class=\"ov-tab") + (x.s == screen_ ? " on" : "") + "\">" + x.name + "</span>";
        }
        t->SetClass("tabs", tabbed);
        t->SetInnerRML(html);
    }
    if (auto* n = doc_->GetElementById("ov-note")) n->SetInnerRML(views::esc(note_));
    if (auto* p = doc_->GetElementById("ov-page")) {
        p->SetInnerRML(h);
        ctx_->Update();
        p->SetScrollTop(scroll);
    }
    renderHints();
    restoreFocus(key);
}

void Overlay::renderHints() {
    if (!doc_) return;
    nav_.promptStyle = model_->buttonPrompts();
    nav_.swapConfirmBack = model_->input.swapConfirmBack;
    const HintLabels h = nav_.hints();
    struct Item { const std::string& key; const char* text; const std::vector<std::string>& pics; };
    const Item items[] = {{h.confirm, "select", h.confirmG}, {h.back, screen_ == Screen::Main ? "resume" : "back", h.backG}};
    std::string html;
    if (listening()) {
        if (auto* g = doc_->GetElementById("hint-group")) g->SetInnerRML("<div class=\"hint\"><span class=\"key\">Press the new button</span><span>Esc or wait to cancel</span></div>");
        return;
    }
    const bool tabbed = screen_ == Screen::Video || screen_ == Screen::Enhance || screen_ == Screen::Controls;
    std::string tabsHtml;
    if (tabbed) {
        if (h.tabsG.empty()) tabsHtml = "<span class=\"key\">" + views::esc(h.tabs) + "</span>";
        else for (auto& g : h.tabsG) tabsHtml += views::glyphHtml(g);
        tabsHtml = "<div class=\"hint\">" + tabsHtml + "<span>tabs</span></div>";
    }
    for (auto& x : items) {
        std::string cap;
        if (x.pics.empty()) cap = "<span class=\"key\">" + views::esc(x.key) + "</span>";
        else for (auto& g : x.pics) cap += views::glyphHtml(g);
        html += "<div class=\"hint\">" + cap + "<span>" + x.text + "</span></div>";
    }
    if (auto* g = doc_->GetElementById("hint-group")) g->SetInnerRML(tabsHtml + html);
}
