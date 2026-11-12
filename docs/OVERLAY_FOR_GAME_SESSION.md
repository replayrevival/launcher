# In-game overlay: what the game side needs to do

For the game session. The launcher side is ready to link; this note says what to call and what we need from the game. Design and
screens: `OVERLAY.md`. The API is one header, `include/ste_overlay_host.h` (no SDL, RmlUi or Direct3D headers; two forward
declarations). Licence of everything here: GPL-2.0-or-later.

## Status (2026-11-12, updated)
- Works: `ste_overlay` (static library) + `overlay_test_host` (D3D11 window with our own test picture). Screens: Paused menu, Video,
  Enhancements, Controllers (ports, device, dead zone, rumble test, **remapping**, button prompts, swap confirm/back), confirm for Return
  to launcher / Quit game. Pad, keyboard and mouse. Screenshots: `screenshots/overlay_d3d11_*.png`.
- **No SDL in the library.** `ste_overlay` links no SDL at all (checked: the test host executable contains no SDL symbols). The overlay
  library is built from `ste_ui_core` (settings, input map, mods, views, RmlUi, FreeType, miniz, nlohmann/json); SDL lives only in the
  launcher's own `launcher_ui`. The game keeps its SDL3 and translates its events into the neutral calls below.
- Not done: anything on the game side; a test with a real game and real pads.

## Decisions from the game session, as implemented
| Point | Implemented |
|---|---|
| Live rows | `config/settings.json` row `"apply"`: button prompts, draw distance, frame rate, V-Sync, variable refresh, field of view, enhancement toggles (next match) are `now`/`match`; **aspect, window mode, resolution, display, render scale are `restart`**. V-Sync is `now`, but the overlay shows what `apply_setting` answers: return `NextMatch` and the badge changes to "next match". Volume and cursor rows do not exist in the launcher yet; tell us the keys and we add them. Please confirm per row. |
| Return to launcher | exit code **20**. The launcher profile has `exit_codes.return_to_launcher = 20`; the launcher treats it as a normal end (no "game stopped" box) and shows its main screen again. Quit game: exit code 0 as before. |
| Input | Neutral `Input` (below). The library never sees SDL. |
| Frame order | Draw last: after the game frame and any upscale / FSR, at back buffer resolution, right before `Present`. `render()` expects the back buffer at its final size. |

## Build
Windows, MSVC. `scripts\build.cmd` once, then `scripts\build_overlay.cmd` (targets `ste_overlay`, `overlay_test_host`). Link
`ste_overlay` plus `ste_ui_core`, RmlUi, FreeType, miniz (CMake does it when you link the target). Needs `d3d11`, `dxgi`, `d3dcompiler`.

## Files the overlay reads at run time
`<resource_dir>\ui\`, `assets\` (fonts, glyphs) and `config\` (`profile.json`, `settings.json`, `controls.json`): the launcher's folders,
shipped next to the game exe. `<data_dir>` is the launcher's data folder (the one that holds `game_settings\ste_video.ini`,
`ste_enhance.ini` and `input_mapping.json`). The overlay writes those same ini files and `input_mapping.json` (atomically, like the
launcher), so launcher and overlay always agree. It does not write outside `<data_dir>` (on first use it may also create the small
launcher marker files there, as the launcher does).

## Calls
```cpp
#include "ste_overlay_host.h"
ste_overlay::InitInfo info;
info.resource_dir = u8"...";  info.data_dir = u8"...";
info.device = d3d11Device;  info.width = w;  info.height = h;  info.content_scale = 1.0f;   // see "Scale" below
info.callbacks.apply_setting = [](const std::string& key, const std::string& value) -> ste_overlay::Applied { ... };
info.callbacks.on_show = pauseGame;  info.callbacks.on_hide = resumeGame;
info.callbacks.return_to_launcher = ...;  // exit(20)
info.callbacks.quit_game = ...;
info.callbacks.rumble = [](unsigned padId, float strength, unsigned ms) { ... };            // optional, Controllers screen "TEST RUMBLE"
std::string err;
auto overlay = ste_overlay::Host::create(info, err);          // render thread
overlay->set_pads({ {guid, name, kind, yourPadHandle}, ... }); // once, and whenever pads connect / disconnect

// input thread, when the game's own ini keys say so (F1 / Guide / Back+Start held for overlay_chord_ms):
overlay->show();
// every input event while overlay->visible(): feed it, do not give it to the game
overlay->feed(ste_overlay::Input{ ... });                      // Nav (Up/Down/Left/Right/Confirm/Back), MouseMove, MouseButton, MouseWheel
overlay->set_input_device(ste_overlay::DeviceKind::Xbox);      // when the player switches between keyboard and pad types

// every frame, input thread:   overlay->update(dt);
// every frame, render thread, after the game's frame (and FSR), right before Present:
overlay->render(backBufferRtv);                                // no-op while hidden
// on window resize:            overlay->resize(w, h, 1.0f);
```
Rules:
- One `Host` per process (RmlUi keeps global state). `create`, `render`, `resize` and destruction on the render thread.
- `render()` draws over whatever the back buffer holds, **saves and restores** the pipeline state it touches and the bound render
  targets, never presents. It needs the immediate context only (taken from the device). It does not need a depth buffer.
- While visible the overlay consumes everything. On `on_hide` release all held keys / buttons in the game so nothing sticks.
- `Input::Nav`: send one event per press and per OS key repeat (the overlay does not auto-repeat from your Nav events; for a held
  D-pad or stick, repeat after about 400 ms, then every 120 ms, like the launcher). Mouse coordinates are back buffer pixels.
- **Remapping.** While `overlay->listening()` is true the Controllers screen waits for a button to bind. Then send raw presses as
  `Input::Type::Source` **instead of** Nav: the SDL3 gamepad button string (`SDL_GetGamepadStringForButton`: "a", "leftshoulder", "start",
  "dpup", ...), "lefttrigger" / "righttrigger" when the trigger passes about 60 %, `"key:" + SDL_GetKeyName(key)` for the keyboard, or
  "cancel" for Esc. (Guide is reserved; do not send it.) It times out after 6 s. The binding is saved to `input_mapping.json` at once.
- `apply_setting(key, value)`: `key` is the launcher's id, `<file>.<key>` ("video.fps_cap", "enhance.unlock_all_monsters"); `value` is what
  was written to the ini ("" = key removed, game default). Return `Now`, `NextMatch`, `AfterRestart` or `Refused`. The overlay already
  saved the value; `Refused` only adds a note. **Special key `input.mapping`** (value = full path of `input_mapping.json`): the player
  changed a binding, dead zone, rumble or port; re-read the file if you can. The badge next to each row follows your answer.
- Scale: `content_scale` is the player's own "UI size" multiplier (1.0 = normal), **not the OS DPI**. The layout is sized from the back
  buffer height (the overlay is as big on 720p as on 4K, relative to the picture), so a DPI change only matters through the new back
  buffer size: call `resize(w, h, scale)` when the swap chain is resized.
- The process should be DPI aware (per-monitor v2): the overlay lists monitors with the Win32 API and reports physical pixels.
- `return_to_launcher`: exit with code 20; `quit_game`: normal exit.

## Open points for the game session
1. Please confirm per row which settings the game can apply while running (the table above is what is shipped now).
2. Volume and cursor rows: the launcher has no such settings yet. If the game has ini keys for them, send the keys, labels and the apply
   level and we add rows to `config/settings.json` (no code needed).
3. The Guide-button / Back+Start chord and F1 are yours (`overlay_key`, `overlay_guide`, `overlay_chord`, `overlay_chord_ms`).
