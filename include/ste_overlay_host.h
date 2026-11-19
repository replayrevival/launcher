#pragma once
// In-game overlay: the API a game links against (docs/OVERLAY.md, "Hosting API").
//
// The overlay is the launcher's own screens (Video, Enhancements, Button prompts) drawn over the game with Direct 3D 11, opened
// from the game with a key, the Guide button or Back+Start held. This header is all a game needs: it includes no SDL, RmlUi or
// Direct3D headers (two forward declarations only), and the game keeps its window, device, swap chain and frame loop.
//
// Threads: create/destroy/render/resize on the render thread; feed/update/show/hide on the thread that owns input (they may be the
// same). One Host per process (RmlUi has process-wide state).
//
// Frame order:  game draws its frame  ->  host->render(backBufferRtv)  ->  Present.
// render() draws on top of whatever the back buffer holds, saves and restores the pipeline state it touches and the bound render
// targets, and never presents. While visible() the host should send all input to feed() instead of the game and call the game's
// pause. On hide, release every held key and button in the game so nothing sticks.
//
// Licence: GPL-2.0-or-later, like the rest of the launcher.
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11RenderTargetView;

namespace ste_overlay {

// What the game could do with a changed setting.
enum class Applied {
    Now,           // the game applied it immediately
    NextMatch,     // used when the next match / level starts
    AfterRestart,  // saved, used the next time the game starts
    Refused,       // the game cannot use this value; the overlay tells the player it is saved for the next start
};

enum class DeviceKind { Keyboard, Xbox, PlayStation, Switch, Generic };

struct Callbacks {
    // A setting changed. `key` is the launcher's key id, "<file>.<key>" ("video.fps_cap", "enhance.unlock_all_monsters"); `value` is
    // the new value as it is written to the ini file, "" when the key went back to the game's default. Called after the ini file
    // was written, once per key of the row. Return what happened; may be empty (then everything counts as AfterRestart).
    std::function<Applied(const std::string& key, const std::string& value)> apply_setting;
    std::function<void()> on_show;  // the overlay opened: pause the game, stop sending input to it
    std::function<void()> on_hide;  // the overlay closed (Back, Resume, or hide()): resume
    std::function<void()> return_to_launcher;  // the player confirmed "Return to launcher": exit with the launcher's exit code
    std::function<void()> quit_game;           // the player confirmed "Quit game"
    // Controller vibration test from the Controllers screen: the `Pad::id` you gave to set_pads(), strength 0..1, milliseconds.
    // Return false if that pad cannot rumble. May be empty.
    std::function<bool(unsigned pad_id, float strength, unsigned ms)> rumble;
};

struct InitInfo {
    std::string resource_dir;  // UTF-8: the folder holding ui\, assets\ and config\ (the launcher's folders, next to the game exe)
    std::string data_dir;      // UTF-8: the launcher's data folder: game_settings\ste_*.ini, input_mapping.json live below it
    ID3D11Device* device = nullptr;  // the game's device (feature level 10 or better, needs ID3D11Device1: Windows 8+)
    int width = 0, height = 0;       // back buffer size in pixels
    float content_scale = 1.f;       // user / OS scale; the layout is sized from the height on top of this
    Callbacks callbacks;
};

// Input in neutral form so the game does not have to use SDL. Coordinates are back buffer pixels.
enum class Nav { Up, Down, Left, Right, Confirm, Back, PrevTab, NextTab };  // PrevTab/NextTab = LB/RB (0.1.8; appended, older games never send them)
struct Input {
    enum class Type { Nav, MouseMove, MouseButton, MouseWheel, Source, NavRelease } type = Type::Nav;  // NavRelease (0.1.8): the Nav button was released; after the first one the overlay repeats a held Nav itself, so send only the first press
    Nav nav = Nav::Up;     // Type::Nav: a key / D-pad / stick step / A / B press; send key repeats too (not releases)
    float x = 0, y = 0;    // MouseMove, MouseButton: pointer position
    int button = 0;        // MouseButton: 0 left, 1 right, 2 middle
    bool down = false;     // MouseButton
    float wheel = 0;       // MouseWheel: notches, positive = up
    // Source (only while Host::listening()): the raw press to bind, in SDL3's names: a gamepad button string
    // (SDL_GetGamepadStringForButton: "a", "leftshoulder", "start", "dpup", ...), "lefttrigger" / "righttrigger" when the trigger passes
    // about 60 %, "key:<SDL_GetKeyName>" for the keyboard, or "cancel" (Esc). Do not also send it as Nav.
    std::string source;
};

// A connected controller, for the Controllers screen.
struct Pad {
    std::string guid;                          // SDL_GUIDToString(SDL_GetGamepadGUIDForID(id)) or any stable id of the device model
    std::string name;
    DeviceKind kind = DeviceKind::Generic;
    unsigned id = 0;                           // your own handle, passed back in Callbacks::rumble (0 = none)
};

class Host {
public:
    // Starts RmlUi, loads the schemas and fonts, builds the overlay. Returns null and fills `err` (plain language) on failure.
    static std::unique_ptr<Host> create(const InitInfo& info, std::string& err);
    ~Host();

    void show();  // opens at the main menu (calls on_show)
    void hide();  // calls on_hide
    bool visible() const;

    // True when the overlay used the event. While visible() the game gets nothing, whatever this returns.
    bool feed(const Input& in);
    // Which pad / keyboard the player is using, so the hint bar shows matching buttons (see "Button prompts" in the launcher).
    void set_input_device(DeviceKind kind);
    // Connected controllers in connection order; call when they change and once before show(). The first one fills port 1, and so on
    // (the player can pick other devices per port). Without any, the Controllers screen says "No controller found".
    void set_pads(const std::vector<Pad>& pads);
    // True while the Controllers screen waits for a button to bind: send raw presses as Input::Type::Source instead of Nav.
    bool listening() const;

    void update(double seconds);                        // once per frame (key repeat, animation)
    void render(ID3D11RenderTargetView* back_buffer);   // no-op while hidden
    void resize(int width, int height, float content_scale);

private:
    Host();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ste_overlay
