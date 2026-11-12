#pragma once
// The in-game overlay (docs/OVERLAY.md): the launcher's Video / Enhancements / Button prompts rows on a dimmed game picture,
// usable with pad, keyboard and mouse. No window, no GL/D3D: the host owns the window, the device and the frame loop, creates
// RmlUi with its own render backend and loads the fonts, then drives this class. One instance per process.
#include <RmlUi/Core.h>

#include <filesystem>
#include <functional>
#include <string>

#include "core/launcher_model.h"
#include "ui/Navigation.h"

enum class Applied { Now, NextMatch, AfterRestart, Refused };

struct OverlayCallbacks {
    // A setting key id ("video.fps_cap") changed to `value` ("" = back to the game's default). The game applies it if it can.
    std::function<Applied(const std::string& key, const std::string& value)> applySetting;
    std::function<void()> onShow, onHide;  // the game pauses / resumes
    std::function<void()> returnToLauncher;
    std::function<void()> quitGame;
};

struct OverlayInit {
    std::filesystem::path resourceDir;  // folder with ui\ and assets\ (next to the game exe)
    int width = 1280, height = 720;     // back buffer size in pixels
    float contentScale = 1.f;           // user / OS scale on top of height / 820
    float mouseScale = 1.f;             // pixels per SDL window coordinate (SDL_GetWindowSizeInPixels / SDL_GetWindowSize)
};

class Overlay : public Rml::EventListener {
public:
    ~Overlay() override { shutdown(); }
    // `model` must be loaded (LauncherModel::load) and outlive the overlay. Rml::Initialise and the fonts are the host's job.
    bool init(LauncherModel& model, const OverlayInit& init, OverlayCallbacks cb, std::string& err);
    void shutdown();

    void show();
    void hide();
    bool visible() const { return visible_; }

    // Raw input from the host. While visible the overlay consumes every input event (returns true) so the game gets none;
    // while hidden it returns false. The host decides when F1 / Guide / Back+Start opens it.
    bool feedEvent(const SDL_Event& ev);
    void update(double dt);  // once per frame
    void render();           // after the game's frame, inside the host's backend begin/end; no-op while hidden
    void resize(int width, int height, float contentScale, float mouseScale);

    Navigation& navigation() { return nav_; }  // tests: pin the device kind, push actions
    Rml::Context* context() { return ctx_; }

    void ProcessEvent(Rml::Event& ev) override;

private:
    enum class Screen { Main, Video, Enhance, Controls, ConfirmLauncher, ConfirmQuit };
    void go(Screen s);
    void renderScreen();
    void renderHints();
    void act(const std::string& action, const std::string& arg);
    void step(const std::string& id, int dir);
    void notifyGame(const std::string& rowId);
    void handleNav(NavAction a);
    void restoreFocus(const std::string& key);
    std::string focusKey() const;

    LauncherModel* model_ = nullptr;
    OverlayInit init_;
    OverlayCallbacks cb_;
    Navigation nav_;
    Rml::Context* ctx_ = nullptr;
    Rml::ElementDocument* doc_ = nullptr;
    Screen screen_ = Screen::Main;
    bool visible_ = false;
    std::string note_;
};
