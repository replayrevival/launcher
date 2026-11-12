#include "ui/Overlay.h"

#include <SDL3/SDL.h>

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
    init_ = init;
    cb_ = std::move(cb);
    views::setGlyphDir(pathToUtf8(init.resourceDir / "assets" / "glyphs"));
    ctx_ = Rml::CreateContext(kCtx, Rml::Vector2i(init.width, init.height));
    if (!ctx_) { err = "Could not create the overlay screen"; return false; }
    resize(init.width, init.height, init.contentScale, init.mouseScale);
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
}

void Overlay::resize(int w, int h, float contentScale, float mouseScale) {
    init_.width = w;
    init_.height = h;
    init_.contentScale = contentScale;
    init_.mouseScale = mouseScale;
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
    visible_ = false;
    if (cb_.onHide) cb_.onHide();
}

bool Overlay::feedEvent(const SDL_Event& ev) {
    if (!visible_ || !ctx_) return false;
    switch (ev.type) {
        case SDL_EVENT_MOUSE_MOTION:
            ctx_->ProcessMouseMove(int(ev.motion.x * init_.mouseScale), int(ev.motion.y * init_.mouseScale), 0);
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int b = ev.button.button == SDL_BUTTON_LEFT ? 0 : ev.button.button == SDL_BUTTON_RIGHT ? 1 : 2;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) ctx_->ProcessMouseButtonDown(b, 0);
            else ctx_->ProcessMouseButtonUp(b, 0);
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            ctx_->ProcessMouseWheel(Rml::Vector2f(-ev.wheel.x, -ev.wheel.y), 0);
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            nav_.handleEvent(ev);
            return true;  // consumed either way: the game must not see it
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            nav_.handleEvent(ev);
            return false;  // the host still wants to know about pads coming and going
        default: return false;
    }
}

void Overlay::update(double dt) {
    if (!visible_ || !ctx_) return;
    nav_.update(dt);
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
        step(f->GetAttribute<Rml::String>("data-arg", ""), a == NavAction::Right ? +1 : -1);
        return;
    }
    if (a == NavAction::Back) {
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
    } else if (action == "step") {  // chevrons: "<row id>@<-1|1>"
        const size_t at = arg.rfind('@');
        if (at != std::string::npos) step(arg.substr(0, at), std::atoi(arg.c_str() + at + 1) < 0 ? -1 : +1);
    } else if (action == "cycle") {  // click on the value
        step(arg, +1);
    }
}

// One row stepped (chevron, click on the value, or Left/Right): the model changes and saves it, the game is told.
void Overlay::step(const std::string& id, int dir) {
    const auto r = model_->stepSetting(id, dir, SDL_GetTicks());
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
            for (auto& k : keys) {
                const auto v = model_->settings.value(k);
                if (cb_.applySetting(k, v ? *v : "") == Applied::Refused) refused = true;
            }
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
        case Screen::Video: title = "VIDEO"; h = views::settingsRows(*model_, "video", true); break;
        case Screen::Enhance: title = "ENHANCEMENTS"; h = views::settingsRows(*model_, "enhance", true); break;
        case Screen::Controls:
            title = "CONTROLLERS";
            h = views::settingsRows(*model_, "controls", true) +
                "<p class=\"muted small\" style=\"padding-left: 26dp; margin-top: 12dp\">Remapping buttons is in the launcher (Controllers tab).</p>";
            break;
        case Screen::ConfirmLauncher:
        case Screen::ConfirmQuit:
            title = screen_ == Screen::ConfirmLauncher ? "RETURN TO LAUNCHER" : "QUIT GAME";
            h = "<p class=\"ov-text\">Leave the game? Unsaved progress is lost.</p><div class=\"ov-menu\">" +
                navButton("NO, KEEP PLAYING", "ov-go", "main", "ov-no", true) + navButton("YES, LEAVE", "ov-yes", "", "ov-yes") + "</div>";
            break;
    }
    if (auto* t = doc_->GetElementById("ov-title")) t->SetInnerRML(views::esc(title));
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
    for (auto& x : items) {
        std::string cap;
        if (x.pics.empty()) cap = "<span class=\"key\">" + views::esc(x.key) + "</span>";
        else for (auto& g : x.pics) cap += views::glyphHtml(g);
        html += "<div class=\"hint\">" + cap + "<span>" + x.text + "</span></div>";
    }
    if (auto* g = doc_->GetElementById("hint-group")) g->SetInnerRML(html);
}
