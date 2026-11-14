# Interface assumptions (derived, read-only)

The game project's `docs/LAUNCHER_INTERFACE.md` is the authoritative contract between game and launcher. This file records the assumptions the launcher was first written against the launcher codes against. **When LAUNCHER_INTERFACE.md appears: diff it against this file, switch to it, and list every mismatch in CHECKPOINT.md.**

Sources: `mission-l6\play.bat`, `play_max.bat`, `play_low.bat`, `mission-l6\artifacts\{max,low}\ste_*.ini`, `ste_video.ini.sample`, `ste_enhance.ini.sample`, `docs\formats\mod_manifest.md`. Read 2026-10-31.

## 1. Starting the game (from the .bat files)
- Exe: `mission-l6\artifacts\ste_recomp_mission_l6_candidate242.exe` (name changes per candidate -> `profile.json` `game.exe`, resolved relative to a build folder the user/profile sets).
- Working directory: the exe's folder (`artifacts`). No command-line arguments.
- stdout/stderr are redirected to `logs\play_<yyyyMMdd-HHmmss>.out/.err`; the bats then print the exit code. The launcher does the same into its data dir and uses the exit code + last stderr lines for the "it failed" message.
- Environment set by every bat: `RECOMP_AC97_READY=1`, `RECOMP_VBLANK=1`, `RECOMP_KERNEL_LOG_BUDGET=0`, `STE_SHOT_SEC=0`, `STE_PB_LOG_EVERY=3000`, `RECOMP_FB_WINDOW=` (empty), `RECOMP_APU_DSP_ACK=` (empty).
- Max and Low also set `STE_VIDEO_INI=<abs path>`, `STE_ENHANCE_INI=<abs path>` and `STE_KELVIN_CULL=2`. Low additionally intends `STE_TEXPACKS=0` (see mismatch M1).
- Optional (testing only): `STE_PAD_SCRIPT`, `STE_MODS=0`, `STE_MODS_DIR`, `STE_MODS_ENABLE`, `STE_INPUT_PORT1..4`, `STE_ENHANCE_<KEY>`, `STE_VIDEO_*`.

**Launcher plan:** keep the exe-adjacent ini files untouched; write the active settings to launcher-owned `ste_video.ini` / `ste_enhance.ini` and pass them with `STE_VIDEO_INI` / `STE_ENHANCE_INI`, exactly as the Max/Low bats do. The "baseline env" above lives in `profile.json` `launch.env`.

## 2. Presets (file = preset)
| Preset | Source | Meaning |
|---|---|---|
| Original | `play.bat`: ini next to the exe (`artifacts\ste_video.ini`, `ste_enhance.ini`) | **Neither file exists today**, so the game runs on built-in defaults (= all keys at the .sample defaults, all enhancements 0). Original = empty/default ini. |
| Max | `artifacts\max\` | video: 1920x1080 window, aspect=wide, menu_aspect=pillar, fps_cap=60, vsync=off, render_ahead=1, draw_distance=unlimited, fog_scale=4, fov_scale=1, fov_min_zoom=0.7854, cull_margin=1, anisotropy=16, mip_bias=0. enhance: unlock_all_monsters=1, unlock_all_cities=1, unlock_all_challenges=1. |
| Low-end | `artifacts\low\` | video: 1920x1080 window, aspect=wide, menu_aspect=pillar, fps_cap=60, vsync=off, vrr=auto, render_ahead=1, render_scale=0.667, upscale=fsr1, fsr_sharpness=0.25, draw_distance=2, fog_scale=1, fov_scale=1, cull_margin=1, anisotropy=0, mip_bias=0, render_threads=2. enhance: none. |

The launcher ships these three as data in `config/` (copied from the game's files, which are small settings, not game data); "Custom" appears when any value differs.

## 3. ini keys
All keys, ranges and defaults are as in `ste_video.ini.sample` and `ste_enhance.ini.sample` (`[video]`, `[input]` keyboard, `[enhance]`, `[saves]` seed_from_game_dir, `[input]` port1..4). Mockup rows map to keys:

| Launcher row | Key(s) |
|---|---|
| Resolution / Window | `width`,`height` / `mode` (window, borderless, fullscreen) |
| Gameplay view / Menu screens | `aspect` (4:3-pillar, stretch, wide) / `menu_aspect` (pillar, stretch, wide) |
| Render scale | `render_scale` 0.25..1.0, `upscale` (bilinear, fsr1), `fsr_sharpness` |
| Frame rate | `fps_cap` (60, 30, 0), `vsync`, `vrr` (auto, off) |
| Draw distance / FOV / fog | `draw_distance`, `fov_scale`, `fov_min_zoom`, `fog_scale`, `cull_margin` |
| Texture filtering | `anisotropy` (0,2,4,8,16), `mip_bias` |
| Unlocks | `unlock_all_monsters`, `unlock_all_cities`, `unlock_all_challenges`, `unlock_gallery`, `unlock_final_wars`, `unlock_biollante` |
| Gameplay rules | `stem_rule_player_size` (1..1000 %), `stem_footstep_camera_shake`, `stem_rule_rage_meter` (0..5), `stem_more_ai_vehicles`, `stem_no_juggle_limit`, `stem_rule_empty_arena`, `stem_no_knockback_scaling`, `stem_no_min_health`, `stem_no_results_dim`, `stem_weapon_vertical_knockback` |
| Cheats | `cheat_invulnerable_p1/_com`, `cheat_super_damage_com`, `cheat_red_p1/_com` |
| Ports | `[input] port1..4` = auto, xinput0..3, keyboard, none |

Writer rules: preserve comments and unknown keys, change only keys we manage, atomic write.

## 4. Mods
Per `mod_manifest.md`: `mods\` next to the exe (or `STE_MODS_DIR`), one folder per mod with `manifest.json`, `order.json` for order/enabled, the game writes `mods\last_report.txt` at each start. "Works with this build": `game_hashes` must contain the user's verified `default.xbe` SHA-256, `api` must equal 1, dependencies/conflicts resolvable. The manifest has no build/version field, so the mockup's "OLDER BUILD" badge can only be derived from `api` / `game_hashes` mismatch (see M4).

## 5. Game identification
`default.xbe` SHA-256 `6966cc8a3a22d420d05362f24f3e68d7a462c481861189855f05583c0d6de500` (STE retail Xbox; from mod_manifest.md). Disc image = XISO ("MICROSOFT*XBOX*MEDIA" at 0x10000, as in GAME_LIBRARY.md); folder = `default.xbe` at its root.

## Mismatches and gaps (flag to the game session)
- **M1** `play_low.bat` contains the literal text ``set STE_KELVIN_CULL=2`r`nset STE_TEXPACKS=0`` on one line (a PowerShell escape that never became a newline). Result: `STE_KELVIN_CULL` = `2`r`nset STE_TEXPACKS=0` and `STE_TEXPACKS` is never set. (`play.bat` has the same artefact inside a `rem` line, harmless.) The launcher assumes the intent: `STE_KELVIN_CULL=2` and `STE_TEXPACKS=0` for Low-end.
- **M2** Where the game finds its own files is not in the bats. No argument or env var for the user's game path is documented (`STE_GAME_DIR` etc. not found). The launcher needs: how to hand over the verified disc image / folder.
- **M3** Input: the game's `[input]` ports only know XInput (`xinput0..3`), keyboard, none. The launcher's SDL3 features (PlayStation/Switch/generic pads, per-port remapping, per-GUID profiles, deadzone, rumble) need a way to reach the game (a mapping file, or the launcher feeding input). Not documented; remapping UI is blocked on this.
- **M4** `STE_VIDEO_INI` / `STE_ENHANCE_INI` appear only in the bats, not in the .sample comments.
- **M5** The mockup meta line says "build c206", while the bats run candidate242 and the exe name changes per candidate. The launcher needs a build id source (exe name? a version resource? a file).
- **M6** The mockup's launcher footer says "Open source (GPL)"; the project licence is not decided yet (FreeType GPLv2 option, CREDITS.md).

## Update 2026-11-05 (v0.1.2: video options, monitor detection)
- **aspect:** the launcher offers only `4:3-pillar` ("Original (4:3)") and `wide` ("Fit my screen", default). `stretch` is never written; an old `aspect=stretch` is migrated to `wide` on load.
- **menu_aspect:** no setting any more; the launcher always writes `menu_aspect=pillar` (menus keep 4:3, sides show the wider 3D background).
- **Screen choices are independent of presets:** `width`, `height`, `mode`, `display` are not part of Original/Max/Low and survive a preset change. Default resolution = the monitor's native desktop mode (explicit `width`/`height`).
- **NEW KEY REQUESTED (game session, please confirm):** `[video] display = <n>` (integer, 0-based index of the monitor in SDL's `SDL_GetDisplays` order, i.e. the order the launcher lists them; the launcher writes it on every save). Meaning: the monitor the game window opens on / goes borderless-fullscreen on. Absent = the game's current behaviour (primary monitor). The launcher writes it today; the game ignores unknown keys, so nothing breaks until it is implemented. If the game's monitor order differs from SDL's, tell the launcher which identifier to write instead (e.g. device name).
- **Refresh rate:** shown in the resolution list only in Fullscreen mode (from SDL's mode list); no ini key is written for it (none exists).
- Sizes are physical pixels: SDL reports desktop/fullscreen modes in device pixels (the launcher also multiplies by `pixel_density` for platforms that use points). A 4K monitor at 150 % scaling lists 3840 x 2160.

## Update 2026-10-31 (after the maintainer's answers)
- **Game path:** the launcher sets `STE_GAME_PATH=<folder or .iso/.xiso>` (profile `launch.game_path_env`) until LAUNCHER_INTERFACE.md says otherwise. The current candidate ignores it (it never mentions it in its log), so M2 is open on the game side.
- **Build id / exe name:** the launcher shows `build <ProductVersion>` read from the exe's VERSIONINFO (Windows). candidate242 has none, so the launcher shows `build unknown`. `LAUNCHER_GAME_EXE` overrides the program path for development. Closes M5 on the launcher side; waiting for the game session's stable name and version string (format of an 'embedded version string' that is not VERSIONINFO is unknown).
- **Verified in practice (2026-10-31):** launched candidate242 from the launcher with MAX. Its log shows `[VIDEO] ini=...\data\game_settings\ste_video.ini ... aspect=2 render=1920x1080 ... menu_aspect=0`, so `STE_VIDEO_INI` is honoured (M4 confirmed for video). `STE_ENHANCE_INI` was set the same way but the log does not show it being read.
- **Region:** both Xbox games' certificates carry region flags 0x80000007 (all regions), so the launcher says `region free`; it cannot tell NTSC from PAL by the certificate.

## Update 2026-11-11: Button prompts (launcher -> game)
- Setting "Button prompts" on the launcher's Controllers tab. Values: `auto` (match the connected controller, default) | `xbox` | `xbox_original` (Black and White buttons) | `playstation`.
- Written by the launcher to `ste_enhance.ini` as `[input] button_prompts=<value>`; also passed at every launch as env `STE_BUTTON_PROMPTS=<value>` (profile.json `launch.button_prompts_env`; the env wins over the ini, as for the other overrides). The one-off "original settings" retry also passes the player's choice.
- The launcher's own hint bar and remap labels follow the same setting (keyboard use always shows key names).
- TO ADD to the game project's `docs/LAUNCHER_INTERFACE.md` (that file belongs to the game session; the launcher does not edit it): the key, its values and the env var above.

## Capabilities (proposal, 2026-11-12) - what the launcher expects for HD art
The launcher hides settings rows that need a game feature (`"needs": "<feature>"` in `config/settings.json`) until the game reports the feature.
- **How the game reports it:** a file `capabilities.json` in the same folder as the game program: `{"features": ["hd_art"]}`. A missing or unreadable file means no extra features. (Tests: env `LAUNCHER_GAME_CAPS=hd_art`.) If the game session prefers another channel (for example `ste-recomp.exe --capabilities` printing the same JSON), tell us and only `LauncherModel::loadCapabilities()` changes.
- **HD art rows** (Video tab, group "HD ART", feature `hd_art`) and the ini keys they write to `ste_video.ini` `[video]`. Names of the four category keys are our guess, please confirm or correct:
  `hd_art` (0/1, default 0), `hd_art_sky`, `hd_art_ground`, `hd_art_roofs`, `hd_art_billboards` (0/1, default 1 each, only used while `hd_art=1`).
- Presets do not touch these keys (`ignore_for_preset`), so choosing MAX or ORIGINAL never switches HD art on or off.

## Launcher settings win over environment overrides (2026-11-13)
The launcher writes its saved choices into `ste_enhance.ini` / `ste_video.ini` and passes them to the game on start, including `button_prompts` (also as `STE_BUTTON_PROMPTS`). A `STE_BUTTON_PROMPTS` set in the shell before starting the launcher is therefore replaced by the launcher's saved value. This is intended: the launcher is the source of truth for players. For testing, set the value in the launcher (or its ini), not in the environment.

## Volume and mouse pointer rows (2026-11-14, game build c376, LAUNCHER_INTERFACE.md "0.1.4 additions")
Confirmed by the game session: `[audio] master_volume` (0..100, default 100) and `[window] hide_cursor` (auto | always | never, default auto), both in the ini named by `STE_VIDEO_INI` (`ste_video.ini`), both applied at once when the overlay changes them. Schema ids are `audio.master_volume` and `window.hide_cursor` (the names the game answers to); because the id prefix is a section here and not a file, a key may carry `"f": "video"` to name its ini file (`Settings::loadSchema`). Rows are on the Video page under SOUND AND POINTER (not gated: the game does not advertise them in `capabilities.json`; an older game ignores the keys). They are in `ignore_for_preset`, so presets never change them.
