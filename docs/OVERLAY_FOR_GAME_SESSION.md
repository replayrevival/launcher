# In-game overlay: what the game side needs to do

For the game session. The launcher side is ready to link; this note says what to call and what we need from the game. Design and
screens: `OVERLAY.md`. The API is one header, `include/ste_overlay_host.h` (no SDL, RmlUi or Direct3D headers; two forward
declarations). Licence of everything here: GPL-2.0-or-later.

## Status (2026-11-12)
- Works: `ste_overlay` (static library) + `overlay_test_host` (D3D11 window with our own test picture). Screens: Paused menu, Video,
  Enhancements, Controllers (Button prompts only), confirm for Return to launcher / Quit game. Pad, keyboard and mouse. Screenshots:
  `screenshots/overlay_d3d11_*.png`.
- Not done: remapping buttons inside the overlay (comes next), anything on the game side.

## Build
Windows, MSVC. `scripts\build.cmd` once, then `scripts\build_overlay.cmd` (targets `ste_overlay`, `overlay_test_host`). Link
`ste_overlay` (it pulls in `launcher_ui`, RmlUi, FreeType, miniz, nlohmann/json and **SDL3 statically**; the overlay uses SDL only for
its event structs and a tick counter, it does not open a window or start SDL video). Needs `d3d11`, `dxgi`, `d3dcompiler`.
Tell us if the static SDL3 collides with the game's own; we can split it out.

## Files the overlay reads at run time
`<resource_dir>\ui\`, `assets\` (fonts, glyphs) and `config\` (`profile.json`, `settings.json`, `controls.json`): the launcher's folders,
shipped next to the game exe. `<data_dir>` is the launcher's data folder (the one that holds `game_settings\ste_video.ini`,
`ste_enhance.ini` and `input_mapping.json`). The overlay writes those same ini files (atomically, like the launcher), so launcher and
overlay always agree. It does not write outside `<data_dir>` (on first use it may also create the small launcher marker files there, as the launcher does).

## Calls
```cpp
#include "ste_overlay_host.h"
ste_overlay::InitInfo info;
info.resource_dir = u8"...";  info.data_dir = u8"...";
info.device = d3d11Device;  info.width = w;  info.height = h;  info.content_scale = dpiScale;
info.callbacks.apply_setting = [](const std::string& key, const std::string& value) -> ste_overlay::Applied { ... };
info.callbacks.on_show = pauseGame;  info.callbacks.on_hide = resumeGame;
info.callbacks.return_to_launcher = ...;  info.callbacks.quit_game = ...;
std::string err;
auto overlay = ste_overlay::Host::create(info, err);          // render thread

// input thread, when the game's own ini keys say so (F1 / Guide / Back+Start held for overlay_chord_ms):
overlay->show();
// every input event while overlay->visible(): feed it, do not give it to the game
overlay->feed(ste_overlay::Input{ ... });                      // Nav (Up/Down/Left/Right/Confirm/Back), MouseMove, MouseButton, MouseWheel
overlay->set_input_device(ste_overlay::DeviceKind::Xbox);      // when the player switches between keyboard and pad types

// every frame, input thread:   overlay->update(dt);
// every frame, render thread, after the game's frame and before Present:
overlay->render(backBufferRtv);                                // no-op while hidden
// on window resize:            overlay->resize(w, h, dpiScale);
```
Rules:
- One `Host` per process (RmlUi keeps global state). `create`, `render`, `resize` and destruction on the render thread.
- `render()` draws over whatever the back buffer holds, **saves and restores** the pipeline state it touches and the bound render
  targets, never presents. It needs the immediate context only (taken from the device). It does not need a depth buffer.
- While visible the overlay consumes everything. On `on_hide` release all held keys / buttons in the game so nothing sticks.
- `Input::Nav`: send one event per press and per OS key repeat (the overlay does not auto-repeat from your Nav events). Map the D3D /
  stick directions you already use for menus. Mouse coordinates are back buffer pixels.
- `apply_setting(key, value)`: `key` is the launcher's id, `<file>.<key>` ("video.fps_cap", "enhance.unlock_all_monsters"); `value` is what
  was written to the ini ("" = key removed, game default). Return `Now`, `NextMatch`, `AfterRestart` or `Refused`. The overlay already
  saved the value; `Refused` only adds a note ("saved, used the next time you start the game"). The badges shown next to each row come from
  an optional `"apply"` per row in `config/settings.json` (now / match / restart); please check the proposed table in `OVERLAY.md` and tell
  us the real answers, we change the JSON.
- `return_to_launcher`: the game should exit with the code agreed for it (open question in `OVERLAY.md`; the launcher shows its main
  screen again). `quit_game`: normal exit.
- The process should be DPI aware: the overlay lists monitors with the Win32 API and reports physical pixels.

## Open points for the game session
1. Can the game apply the "now" rows (fps cap, vsync, vrr, aspect, fov, draw distance, prompts) while running? Please answer per key.
2. Exit code for "Return to launcher".
3. Does the game use SDL for input? It does not matter for this API (neutral input), only for how you feed it.
4. Where in the frame do you want the overlay (we assume: after the final composition, before Present).
5. Static SDL3 inside `ste_overlay`: acceptable?
