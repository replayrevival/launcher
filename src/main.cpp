// Launcher entry point: SDL3 window + RmlUi screens, navigable by keyboard, mouse and gamepad.
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/identify.h"
#include "core/paths.h"
#include "platform/UiBackend.h"
#include "ui/App.h"
#include "ui/Overlay.h"
#include "ui/views/rows.h"
#include "ui/ChamferDecorator.h"
#include "ui/Navigation.h"

#include "RmlUi_Platform_SDL.h"

namespace fs = std::filesystem;

namespace {

// 1dp = 1 pixel x the system's display scale (Windows 125 % / 150 % ...) x the user's UI size.
// It does NOT depend on the window size: a bigger window shows more content, not bigger content.
float g_testContentScale = 0.f;  // --content-scale: pretend the display scale is this (tests)

// One scripted step: a navigation action, or a raw SDL input event for testing the remap listener
// ("key:Z" key press, "pad:leftshoulder" gamepad button, "trig:left" / "trig:right" trigger pull).
struct ScriptStep {
    NavAction action = NavAction::None;
    std::string raw;
};

// Command line, mostly for automated checks.
struct Args {
    std::string shot;               // --shot <png>: save a screenshot when the script is done, then exit
    std::vector<ScriptStep> script;  // --script up,confirm,wait,play: inject actions ("wait" pauses)
    std::string drop;               // --drop <path>: behave as if this file/folder was dropped on the window
    double hold = 0.3;              // --hold <seconds>: wait this long after the script before the screenshot
    bool killGame = false;          // --kill-game: stop the game when the launcher exits
    bool fresh = false;             // --fresh: ignore the remembered game (first-start screen)
    bool overlay = false;           // --overlay: open the in-game overlay (docs/OVERLAY.md) on top of the launcher (tests)
    std::string device;             // --device xbox|playstation|switch|generic|keyboard: pretend this input device is in use (tests)
    std::string fakeDisplays;       // --fake-displays <json>: use this monitor list instead of the real one (tests)
    int width = 1280, height = 820;
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--shot" && i + 1 < argc) a.shot = argv[++i];
        else if (s == "--drop" && i + 1 < argc) a.drop = argv[++i];
        else if (s == "--hold" && i + 1 < argc) a.hold = std::atof(argv[++i]);
        else if (s == "--kill-game") a.killGame = true;
        else if (s == "--fresh") a.fresh = true;
        else if (s == "--overlay") a.overlay = true;
        else if (s == "--device" && i + 1 < argc) a.device = argv[++i];
        else if (s == "--fake-displays" && i + 1 < argc) a.fakeDisplays = argv[++i];
        else if (s == "--content-scale" && i + 1 < argc) g_testContentScale = float(std::atof(argv[++i]));
        else if (s == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &a.width, &a.height);
        else if (s == "--script" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                NavAction act = NavAction::None;  // "wait" and unknown words just pause
                const bool raw = tok.rfind("key:", 0) == 0 || tok.rfind("pad:", 0) == 0 || tok.rfind("trig:", 0) == 0 ||
                                 tok.rfind("mclick:", 0) == 0 || tok.rfind("wheel:", 0) == 0 || tok.rfind("report:", 0) == 0 || tok.rfind("audit", 0) == 0;
                if (!raw) Navigation::parseAction(tok, act);
                a.script.push_back({act, raw ? tok : std::string()});
            }
        }
    }
    return a;
}

void updateScale(Rml::Context* ctx, SDL_Window* win, float uiSize) {
    float s = g_testContentScale > 0 ? g_testContentScale : SDL_GetWindowDisplayScale(win);
    if (s <= 0.f) s = 1.f;
    ctx->SetDensityIndependentPixelRatio(s * uiSize);
}

// Pad-only audit of the current screen (developer tool, script token "audit"): is something focused, and can every focusable
// element be reached from it with Up / Down / Left / Right alone? Prints one line per screen to stderr.
void auditNav(Rml::Context* ctx, Rml::ElementDocument* doc, Navigation& nav, const std::string& label) {
    Rml::ElementList all;
    doc->QuerySelectorAll(all, ".modal .nav");
    const bool modal = !all.empty();
    if (!modal) doc->QuerySelectorAll(all, ".nav");
    std::vector<Rml::Element*> items;
    for (auto* e : all)
        if (e->IsVisible(true) && !e->HasAttribute("disabled")) items.push_back(e);
    auto key = [](Rml::Element* e) {
        const std::string k = e->GetAttribute<Rml::String>("data-key", "");
        return k.empty() ? e->GetTagName() + "#" + e->GetId() : k;
    };
    Rml::Element* start = ctx->GetFocusElement();
    bool focusOk = start && std::find(items.begin(), items.end(), start) != items.end();
    if (!focusOk && !items.empty()) {  // nothing sensible focused: the first Up/Down press picks one, like in the app
        nav.perform(ctx, doc, NavAction::Down);
        start = ctx->GetFocusElement();
        focusOk = start && std::find(items.begin(), items.end(), start) != items.end();
    }
    std::set<Rml::Element*> seen;
    std::vector<Rml::Element*> todo;
    if (start) { seen.insert(start); todo.push_back(start); }
    while (!todo.empty()) {
        Rml::Element* e = todo.back();
        todo.pop_back();
        for (NavAction a : {NavAction::Up, NavAction::Down, NavAction::Left, NavAction::Right}) {
            e->Focus();
            if (!nav.perform(ctx, doc, a)) continue;  // Left/Right on a value row belong to the app (they change the value)
            Rml::Element* n = ctx->GetFocusElement();
            if (n && n != e && !seen.count(n) && std::find(items.begin(), items.end(), n) != items.end()) { seen.insert(n); todo.push_back(n); }
        }
    }
    std::string missing;
    for (auto* e : items)
        if (!seen.count(e)) missing += (missing.empty() ? "" : ", ") + key(e);
    if (start) start->Focus();
    std::fprintf(stderr, "AUDIT %s%s: %zu focusable, first focus %s, reached %zu%s%s\n", label.c_str(), modal ? " (message box)" : "", items.size(),
                 focusOk ? "ok" : "MISSING", seen.size(), missing.empty() ? "" : ", UNREACHABLE: ", missing.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "Could not start SDL: %s\n", SDL_GetError());
        return 1;
    }

    const char* base = SDL_GetBasePath();
    const fs::path exeDir = base ? fs::path(base) : fs::current_path();

    Profile profile;
    std::string err;
    if (!loadProfile(pathToUtf8(exeDir / "config" / "profile.json"), profile, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }

    const char* pref = SDL_GetPrefPath("recomp-launcher", profile.gameId.c_str());
    App::Paths paths{exeDir, dataDir(exeDir, pref ? fs::path(pref) : exeDir / "data")};

    auto backend = UiBackend::createGL3();
    if (!backend->init((profile.gameTitle + " launcher").c_str(), args.width, args.height, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        SDL_Quit();
        return 1;
    }

    Rml::SetSystemInterface(backend->systemInterface());
    Rml::SetRenderInterface(backend->renderInterface());
    if (!Rml::Initialise()) {
        std::fprintf(stderr, "Could not start the UI library\n");
        return 1;
    }
    registerDecorators();

    views::setGlyphDir(pathToUtf8(exeDir / "assets" / "glyphs"));
    const std::string fontDir = pathToUtf8(exeDir / "assets" / "fonts") + "/";
    Rml::LoadFontFace(fontDir + "PixelifySans-Bold.ttf", false, Rml::Style::FontWeight::Bold);
    const struct { const char* file; int weight; } chakra[] = {
        {"ChakraPetch-Regular.ttf", 400}, {"ChakraPetch-Medium.ttf", 500},
        {"ChakraPetch-SemiBold.ttf", 600}, {"ChakraPetch-Bold.ttf", 700}};
    for (auto& f : chakra) Rml::LoadFontFace(fontDir + f.file, false, Rml::Style::FontWeight(f.weight));

    int pw = args.width, ph = args.height;
    SDL_GetWindowSizeInPixels(backend->window(), &pw, &ph);
    Rml::Context* ctx = Rml::CreateContext("main", Rml::Vector2i(pw, ph));
    updateScale(ctx, backend->window(), 1.f);
    SDL_SetWindowMinimumSize(backend->window(), 800, 560);

    Rml::ElementDocument* doc = ctx->LoadDocument(pathToUtf8(exeDir / "ui" / "rml" / "main.rml"));
    if (!doc) {
        std::fprintf(stderr, "Could not load the launcher screen\n");
        return 1;
    }
    doc->Show();

    Navigation nav;
    if (!args.device.empty()) {
        static const std::pair<const char*, DeviceKind> kinds[] = {{"keyboard", DeviceKind::Keyboard}, {"xbox", DeviceKind::Xbox},
            {"playstation", DeviceKind::PlayStation}, {"switch", DeviceKind::Switch}, {"generic", DeviceKind::Generic}};
        for (auto& k : kinds)
            if (args.device == k.first) nav.forceDevice(k.second);
    }
    App app(profile, paths, nav, backend->window());
    app.killGameOnExit(args.killGame);
    app.ignoreSavedGame(args.fresh);
    if (!args.fakeDisplays.empty()) app.useFakeDisplays(args.fakeDisplays);
    app.setGpuInfo(backend->gpuInfo());
    bool scaleDirty = false;
    app.setUiScaleListener([&] { scaleDirty = true; });
    if (!app.init(doc, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    updateScale(ctx, backend->window(), app.uiScale());
    if (!args.drop.empty()) app.onDrop(args.drop);

    Overlay overlay;
    if (args.overlay) {
        OverlayInit oi;
        oi.resourceDir = exeDir;
        oi.width = pw; oi.height = ph; oi.contentScale = app.uiScale();
        OverlayCallbacks cb;
        cb.applySetting = [](const std::string& k, const std::string& v) {
            std::fprintf(stderr, "OVERLAY applySetting %s=%s\n", k.c_str(), v.c_str());
            return Applied::Now;
        };
        cb.returnToLauncher = [] { std::fprintf(stderr, "OVERLAY returnToLauncher\n"); };
        cb.quitGame = [] { std::fprintf(stderr, "OVERLAY quitGame\n"); };
        cb.onShow = [] { std::fprintf(stderr, "OVERLAY shown\n"); };
        cb.onHide = [] { std::fprintf(stderr, "OVERLAY hidden\n"); };
        if (!overlay.init(app.model(), oi, cb, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        if (nav.device() != DeviceKind::Keyboard) overlay.navigation().forceDevice(nav.device());
        overlay.show();
    }

    size_t scriptPos = 0;
    int frame = 0, nextScript = 8;
    double doneAt = -1;
    bool running = true, shotTaken = false;
    auto last = std::chrono::steady_clock::now();
    const auto t0 = last;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = false;
            else if (ev.type == SDL_EVENT_DROP_FILE) app.onDrop(ev.drop.data ? ev.drop.data : "");
            else if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                backend->setViewport(ev.window.data1, ev.window.data2);
                ctx->SetDimensions(Rml::Vector2i(ev.window.data1, ev.window.data2));
                overlay.resize(ev.window.data1, ev.window.data2, app.uiScale());
                updateScale(ctx, backend->window(), app.uiScale());
            } else if (ev.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                updateScale(ctx, backend->window(), app.uiScale());  // moved to a monitor with another Windows scaling
            } else if (overlay.visible() && (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                             ev.type == SDL_EVENT_MOUSE_BUTTON_UP || ev.type == SDL_EVENT_MOUSE_WHEEL)) {
                // The overlay is SDL-free: translate the mouse here (keys and pads go through `nav` below and reach it as actions).
                int ww = 1, wh = 1, pw2 = 1, ph2 = 1;
                SDL_GetWindowSize(backend->window(), &ww, &wh);
                SDL_GetWindowSizeInPixels(backend->window(), &pw2, &ph2);
                const float sx = float(pw2) / float(ww), sy = float(ph2) / float(wh);
                if (ev.type == SDL_EVENT_MOUSE_MOTION) overlay.feedMouseMove(int(ev.motion.x * sx), int(ev.motion.y * sy));
                else if (ev.type == SDL_EVENT_MOUSE_WHEEL) overlay.feedMouseWheel(ev.wheel.y);
                else overlay.feedMouseButton(ev.button.button == SDL_BUTTON_LEFT ? 0 : ev.button.button == SDL_BUTTON_RIGHT ? 1 : 2,
                                             ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            } else if (nav.handleEvent(ev)) {
                // consumed
            } else {
                RmlSDL::InputEventHandler(ctx, backend->window(), ev);
            }
        }

        if (scaleDirty) { scaleDirty = false; updateScale(ctx, backend->window(), app.uiScale()); }
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        nav.update(dt);
        last = now;

        // Scripted input: one step every 6 frames; "wait" pauses for 40 frames (e.g. for a game check).
        if (scriptPos < args.script.size() && frame >= nextScript && !app.busy()) {
            const ScriptStep step = args.script[scriptPos++];
            if (!step.raw.empty()) {
                SDL_Event e{};
                const std::string v = step.raw.substr(step.raw.find(':') + 1);
                SDL_Window* win = backend->window();
                int ww = 1, wh = 1, pw2 = 1, ph2 = 1;
                SDL_GetWindowSize(win, &ww, &wh);
                SDL_GetWindowSizeInPixels(win, &pw2, &ph2);
                auto mouseAt = [&](float px, float py) {  // layout pixels -> window coordinates
                    SDL_Event m{};
                    m.type = SDL_EVENT_MOUSE_MOTION;
                    m.motion.windowID = SDL_GetWindowID(win);
                    m.motion.x = px * float(ww) / float(pw2);
                    m.motion.y = py * float(wh) / float(ph2);
                    SDL_PushEvent(&m);
                    return m;
                };
                if (step.raw.rfind("mclick:", 0) == 0) {
                    // mclick:<data-key>:<left|right|value>: a real mouse click on that part of a row
                    const size_t c = v.rfind(':');
                    float x = 0, y = 0;
                    if (c == std::string::npos || !app.elementCenter(v.substr(0, c), v.substr(c + 1), x, y)) {
                        std::fprintf(stderr, "SCRIPT: no such element: %s\n", v.c_str());
                    } else {
                        const SDL_Event m = mouseAt(x, y);
                        for (bool down : {true, false}) {
                            SDL_Event b{};
                            b.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                            b.button.windowID = m.motion.windowID;
                            b.button.button = SDL_BUTTON_LEFT;
                            b.button.down = down;
                            b.button.clicks = 1;
                            b.button.x = m.motion.x;
                            b.button.y = m.motion.y;
                            SDL_PushEvent(&b);
                        }
                    }
                    nextScript = frame + 6;
                    goto stepDone;
                } else if (step.raw.rfind("wheel:", 0) == 0) {
                    // wheel:<n>: n notches down (negative = up) over the option list
                    mouseAt(float(pw2) * 0.5f, float(ph2) * 0.55f);
                    SDL_Event wv{};
                    wv.type = SDL_EVENT_MOUSE_WHEEL;
                    wv.wheel.windowID = SDL_GetWindowID(win);
                    wv.wheel.y = -float(std::atof(v.c_str()));
                    wv.wheel.mouse_x = float(ww) * 0.5f;
                    wv.wheel.mouse_y = float(wh) * 0.55f;
                    SDL_PushEvent(&wv);
                    nextScript = frame + 6;
                    goto stepDone;
                } else if (step.raw.rfind("audit", 0) == 0) {
                    auditNav(ctx, doc, nav, step.raw.size() > 6 ? step.raw.substr(6) : "screen");  // "audit:<label>"
                    nextScript = frame + 6;
                    goto stepDone;
                } else if (step.raw.rfind("report:", 0) == 0) {
                    std::fprintf(stderr, "REPORT scroll=%.1f focus=%s value[%s]=%s\n", app.pageScroll(), app.focusedKey().c_str(), v.c_str(),
                                 app.rowValue(v).c_str());
                    nextScript = frame + 6;
                    goto stepDone;
                }
                if (step.raw[0] == 'k') {
                    e.type = SDL_EVENT_KEY_DOWN;
                    e.key.key = SDL_GetKeyFromName(v.c_str());
                } else if (step.raw[0] == 'p') {
                    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                    e.gbutton.button = Uint8(SDL_GetGamepadButtonFromString(v.c_str()));
                    e.gbutton.down = true;
                } else {
                    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
                    e.gaxis.axis = Uint8(v == "left" ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                    e.gaxis.value = 32000;
                }
                SDL_PushEvent(&e);
                nextScript = frame + 6;
            } else if (step.action == NavAction::None) {
                nextScript = frame + 40;
            } else {
                nav.push(step.action);
                nextScript = frame + 6;
            }
        stepDone:;
        }

        app.update();
        if (overlay.visible()) {
            if (overlay.navigation().device() != nav.device()) overlay.navigation().forceDevice(nav.device());
            for (NavAction a : nav.take()) overlay.feedNav(a);
        }
        overlay.update(dt);
        for (NavAction a : nav.take()) {
            if (nav.perform(ctx, doc, a)) continue;
            app.handleAction(a);
        }
        if (nav.takeDeviceChanged()) app.refreshHints();

        ctx->Update();
        backend->beginFrame();
        ctx->Render();
        overlay.render();
        backend->endFrame();

        if (!args.shot.empty() && !shotTaken && scriptPos >= args.script.size() && frame >= nextScript && !app.busy()) {
            const double t = std::chrono::duration<double>(now - t0).count();
            if (doneAt < 0) doneAt = t;
            if (t - doneAt >= args.hold) {
                const bool ok = backend->captureFrame(args.shot);
                std::fprintf(stderr, ok ? "Saved %s\n" : "Could not save %s\n", args.shot.c_str());
                shotTaken = true;
            }
        }
        backend->present();
        if (shotTaken) running = false;
        ++frame;
    }

    // The App (and so the game process handle) must go before the UI library shuts down.
    Rml::Shutdown();
    backend->shutdown();
    SDL_Quit();
    return 0;
}
