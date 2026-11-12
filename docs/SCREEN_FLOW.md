# Screen flow (v0.1)

Every screen is reachable and fully usable by gamepad, keyboard and mouse. Hint bar (bottom): `A Select  B Back  LB/RB Tabs  Start Play`. Glyphs follow the last-used device type. Option "swap confirm/back" flips A/B (Nintendo layout).

```
 start
   |
   +-- no remembered game, or file missing/changed --> [FIRST START]
   |                                                        |
   |                                      verified OK ------+
   v                                                        v
 [MAIN] <---------------------------------------------------+
   |  Tabs (LB/RB): Overview | Video | Enhancements | Mods | Controllers
   |  Start / PLAY --> [LAUNCHING] --ok--> launcher stays idle/closes (config)
   |                        \--fail--> [ERROR DIALOG] --B--> [MAIN]
   +-- "Change game" (Overview) --> [FIRST START]
```

## FIRST START (add your game)
- Choose file/folder (system dialog), drag and drop onto the window, or **Search drives** (scans for `.iso` and folders containing `default.xbe`; read-only, depth-limited, cancellable).
- Identify: folder -> read `default.xbe`; ISO -> read XISO directory, locate `default.xbe`. SHA-256 compared with `profile.json` `identify.sha256`.
- States and plain-language messages: *Found and verified* / *This is a different game (looks like X)* / *This is the right game but not the version we support* / *Could not read the file (in use, or no permission)* / *Not a disc image*.
- Footer note always shown: "Your file is only read. Nothing is copied or changed."
- Remembered path stored in launcher state; re-verified (size + mtime, hash on change) at each start.

## MAIN
- Title (Pixelify Sans) + big **PLAY**; preset row **Original / Max / Low-end** (one is highlighted; any manual edit shows "Custom").
- Tabs:
  - **Overview**: game, verified status, path, change game, build "works with" info, brand.
  - **Video**: mode, resolution, aspect, fps cap, vsync, render scale/upscale, anisotropy, draw distance, FOV, etc. -> `ste_video.ini`.
  - **Enhancements**: toggles -> `ste_enhance.ini` `[enhance]`; STEM options; saves seed.
  - **Mods**: list with enable checkbox, reorder (up/down; LB/RB-less since LB/RB are tabs: use X/Y or d-pad+hold), install from zip/folder, "works with this build" badge (from `game_hashes`, `game`, `api`), conflicts/dependencies shown. Writes `mods\order.json`. No authoring tools.
  - **Controllers**: 4 ports (maps to `[input] port1..4`), pad per port, remap (select a row, press the button), deadzone, rumble test, swap confirm/back, per-GUID profile, glyph type.
- ini files: only keys we manage are rewritten; comments and unknown keys are preserved. Written atomically (temp + rename).

## LAUNCHING / ERROR
- Spawns the game exe from the profile with args/env; waits briefly to detect immediate exit.
- Errors in plain words with a "Copy details" action: exe missing, couldn't start, exited at once (exit code + last log lines if available).

## Input model
Navigation graph: d-pad/stick/arrows move focus, A/Enter select, B/Esc back, LB/RB/Q/E (or Ctrl+Tab) switch tabs, Start/F5 play, mouse hover+click. RmlUi focus + a small controller-navigation layer. The same layer will drive the in-game overlay.

## Portable mode
`portable.txt` next to the launcher exe -> all launcher state, per-controller profiles and logs in `data\` beside the exe; otherwise the per-user pref dir. The game's own ini files always live next to the game exe.
