# In-game overlay (planned for launcher 0.1.4)

Status: the `Overlay` class (`src/ui/Overlay.*`, `ui/rml/overlay.rml`) is written and works inside the launcher window; the Direct3D 11 backend and the game-side hook are not. See "Refactor progress".

## What the player gets
Press **Guide / Home / PS button** (Xbox, PlayStation, Switch pads) or **F1** while playing. If the system or a store overlay (Game Bar, Steam) takes the Guide button, **hold Back + Start for about one second** does the same. The game's own ini keys for this already exist (`[input] overlay_key`, `overlay_guide`, `overlay_chord`, `overlay_chord_ms`, see the game project's `docs/LAUNCHER_INTERFACE.md`); the overlay host reads them, the launcher does not.

The overlay looks like the launcher (same theme, same hint bar) and is fully usable with pad, keyboard and mouse. While it is open the game is paused and does not receive input.

### Screens
```
OVERLAY
  Resume
  Video            -> the launcher's Video rows
  Enhancements     -> the launcher's Enhancements rows
  Controllers      -> Button prompts, ports, dead zone, rumble, remapping
  Return to launcher   (asks first: "Leave the game? Unsaved progress is lost.")
  Quit game            (asks first)
```
Every settings row shows when a change takes effect:

| Badge | Meaning |
|---|---|
| **applies now** | the game applies it immediately |
| **next match** | applied when the next match / level starts |
| **after restart** | saved now, used the next time the game is started |

Settings are written to the same files the launcher writes (`ste_video.ini`, `ste_enhance.ini`, `input_mapping.json`), so the launcher and the overlay always agree.

### Which setting is live (to be confirmed by the game session)
The launcher cannot know what the running game can change on the fly. Proposal, to be replaced by the game's answer in `LAUNCHER_INTERFACE.md`:

| Setting | Proposed badge |
|---|---|
| fps_cap, vsync, vrr | now |
| aspect (gameplay view), fov_scale, draw_distance, fog_scale, cull_margin | now (camera/culling are per frame) |
| render_scale, upscale, anisotropy, mip_bias, render_threads | after restart |
| width, height, mode, display | after restart (window mode may become "now" later) |
| enhance.* unlocks and cheats | next match |
| Button prompts | now |
| input remapping, dead zone, rumble | now |

The launcher schema (`config/settings.json`) will get an optional per-key `"apply": "now" | "match" | "restart"`. Missing = "after restart". The overlay shows the badge from it; the launcher can show the same badge on its rows later.

## Input
- Open/close: Guide button, F1 (configurable), Back+Start held (`overlay_chord_ms`, default 1000). Closing: the same, or Back/B.
- Navigation is the launcher's `Navigation` class unchanged: it already turns SDL keyboard/gamepad events into `NavAction`s and moves RmlUi focus.
- While open, **all** input events go to the overlay and none to the game; on close, the host releases all pressed keys/buttons so nothing sticks.
- Button hints follow the "Button prompts" setting, like the launcher.

## Hosting API (what the game links against)
The overlay is a static library built from the launcher's UI code, with no window and no GL/D3D assumptions. The game provides the window, the device and the frame loop. Sketch (C++ header `overlay/OverlayHost.h`, not written yet):

```cpp
struct OverlayInit {
    std::filesystem::path resourceDir;     // ui\ assets\ config\ (next to the game exe)
    std::filesystem::path dataDir;         // where the ini files and input_mapping.json live
    UiBackend* backend;                    // see below: D3D11 in the game
    int width, height; float contentScale; // back buffer size and DPI scale
};
enum class Applied { Now, NextMatch, AfterRestart, Refused };
struct OverlayCallbacks {
    std::function<Applied(const std::string& key, const std::string& value)> applySetting; // game applies a live change
    std::function<std::string(const std::string& key)> readSetting;                        // current value, for the rows
    std::function<void()> onShow, onHide;     // game pauses / resumes
    std::function<void()> returnToLauncher;   // game exits with a code the launcher understands
    std::function<void()> quitGame;
};
class Overlay {
public:
    bool init(const OverlayInit&, OverlayCallbacks, std::string& err);
    void shutdown();
    void show(); void hide(); bool visible() const;
    // Host forwards raw input. Returns true when the overlay consumed the event (do not give it to the game).
    bool feedEvent(const SDL_Event&);              // or a small neutral struct if the game does not use SDL events
    void update(double dt);                        // once per frame (key repeat, animations)
    void render();                                 // draws into the host's current frame, on top of the game
    void resize(int w, int h, float contentScale);
};
```
Rules: no globals, one instance per game process, `init`/`render` on the render thread, `feedEvent`/`update` on the thread that owns input. The host decides *when* the chord/key opens the overlay (it already reads the ini keys) and calls `show()`.

### Rendering backend
`src/platform/UiBackend.h` already hides the renderer from the UI (`systemInterface()`, `renderInterface()`, `beginFrame()`, `endFrame()`, viewport). The launcher uses the GL3 implementation. **RmlUi 6.3 ships a Direct3D 11 renderer** (`Backends/RmlUi_Renderer_DX11.cpp`, MIT), so the game needs a thin `UiBackendD3D11` that owns no window: it wraps the game's `ID3D11Device` / context / render target view, saves and restores the pipeline state around `render()`, and never calls `present()`. No new dependency is needed; this is the preferred route. (RmlUi also ships DX12, Vulkan and SDL_GPU renderers for other hosts.)

### Data flow for a setting
```
row change -> Overlay writes the key to the ini (same writer as the launcher, atomic)
           -> callbacks.applySetting(key, value) -> Applied::Now / NextMatch / AfterRestart
           -> row badge updates ("applies now" / ...); AfterRestart shows "Takes effect when you restart the game"
```
"Return to launcher" exits the game process with a documented exit code (proposal: 0 with the launcher running it as a child; the launcher then simply shows its main screen again). "Quit game" exits normally; the launcher recognises it by the overlay's own marker file or exit code, so it does not show the "game stopped" box.

## What exists in the launcher today
- `Navigation` (keyboard + gamepad -> `NavAction`, focus movement) takes an `Rml::Context` and `Rml::ElementDocument`; it has no window code.
- `UiBackend` is abstract; only the GL3 implementation exists (`UiBackend.cpp`).
- Settings model (`core/settings`), input model (`core/input_map`), mods (`core/mods`), display list (`core/display_modes`) have no UI and no SDL window code.
- Rows are generated as RML strings (`rowHtml`, `renderRows`) from `config/settings.json`; the overlay can reuse them.
- `config/controls.json`, `settings.json`, `profile.json` hold everything game-specific.

## Refactor progress (2026-11-12)
- Done: step 1 (`LauncherModel`, `src/core/launcher_model.*`: profile, settings, saved game, monitors and resolutions, input map, mods, `stepSetting()`, `rowAction()`; `App` is a view of it), step 2 (`src/ui/views/`: `rows.*` settings rows, `mods.*` Mods page, `controllers.*` Controllers page; plain functions from model + a small state struct to RML, no SDL and no window), the shared row dispatcher (`LauncherModel::rowAction()` for `step` / `cycle`), step 4's CMake target `launcher_ui` (everything except `main.cpp` and the GL backend; the executable links it), and controller pictures via `views::glyphHtml()` (host sets the folder with `views::setGlyphDir()`).
- Step 3 done: `Overlay` (init with a loaded `LauncherModel`, `show/hide/visible`, `feedEvent`, `update`, `render`, `resize`) with its own RmlUi context and document: Paused menu (Resume, Video, Enhancements, Controllers, Return to launcher, Quit game with a confirm box), the settings rows with the "applies now / next match / after restart" badge from the optional `"apply"` key in `settings.json` (missing = restart), Back closes, the host callbacks `applySetting` / `onShow` / `onHide` / `returnToLauncher` / `quitGame`. Rows go through `LauncherModel::rowAction/stepSetting`, so they save the same ini files as the launcher. Checked inside the launcher with the developer flag `--overlay` (callbacks print `OVERLAY ...` lines); screenshots `docs/screenshots/overlay_*.png` show it over the launcher instead of a game picture.
- Differences from the sketch below: `init` takes the `LauncherModel` (it already holds the data dir and profile) and an `OverlayInit` with `resourceDir`, size, `contentScale`, `mouseScale`; the Overlay creates its own context, so the host only needs `Rml::Initialise`, a render backend and the fonts; `Applied` has four values; mouse events are translated inside `feedEvent`.
- Step 4/5/6 done (2026-11-12): `include/ste_overlay_host.h` (the game-facing API, neutral input, no SDL/RmlUi/D3D headers), `src/overlay_host/ste_overlay_host.cpp`, `UiBackendD3D11` (`src/platform/UiBackendD3D11.*`: RmlUi's DX11 renderer on the host's device, PNG loading, saves/restores the pipeline and render targets) and `tools/overlay_test_host.cpp` (D3D11 window, our own test picture `tests/overlay/test_scene.png`). Build with `scripts\build_overlay.cmd`; screenshots `docs/screenshots/overlay_d3d11_*.png`. For the game session: `OVERLAY_FOR_GAME_SESSION.md`.
- Done later the same day: remapping in the overlay (`ui/ControllersUi.*` holds the port / device / listen logic shared with the launcher's Controllers tab; the host sends raw presses with `Input::Type::Source`), the library no longer contains SDL (`ste_ui_core` + `NavCore`; the game ships its own SDL3), apply answers from the game session (aspect, mode, resolution, display, render scale after restart; V-Sync now unless the game answers next match; the badge follows `apply_setting`), exit code 20 = Return to launcher (`profile.json` `exit_codes`).
- Not done: anything on the game side; showing the badges in the launcher; badges on the monitor/resolution rows; 

## Refactor steps (in this order, each step keeps the launcher working)
1. **Split `App`** into a window-free `LauncherModel` (settings, state, input map, mods, displays, game-process facts) and views. `App` today mixes both in `src/ui/App*.cpp`.
2. **Move the row/page renderers** (`rowHtml`, `renderRows`, the controllers page, the video special rows) into `ui/views/` taking `LauncherModel&` and returning RML.
3. **Add `ui/Overlay.*`** with its own small document (`ui/rml/overlay.rml`) that uses those views, plus the badges (`apply` key in `settings.json`).
4. **`OverlayHost.h`** (API above) as a CMake target `launcher_ui` (static library: core + ui, no `main.cpp`, no GL backend); the launcher executable links it too.
5. **`UiBackendD3D11`** in the game project, based on RmlUi's DX11 renderer.
6. **Test host:** a tiny D3D11 sample window that opens the overlay with F1, to test without the game (lives next to the launcher, not shipped).

## Questions for the game session (answered 2026-11-12: see OVERLAY_FOR_GAME_SESSION.md; per-row confirmation still open)
- Which settings can really change while running (table above).
- Which exit code means "return to launcher", and whether the game wants to keep running its own pause menu underneath.
- Whether the game polls SDL events (then `feedEvent(const SDL_Event&)` works as is) or needs a neutral input struct.
- Where the overlay may draw in the frame (after the game's final composition, before present) and how it protects the game's D3D11 state.
