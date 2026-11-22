# Modding guide

For people who want to change or add to *Godzilla: Save the Earth* on PC (the recompiled game). Players: see [PLAYER_GUIDE.md](PLAYER_GUIDE.md).

**One rule above all: a mod never contains game data.** This project ships no mods and hosts none; you host yours. Your mod holds only your own work, or it is a set of *patches* (only the difference from the player's own file).

Contents: [Start here](#start-here) · [Retexture a monster](#retexture-a-monster) · [Replace sounds and music](#replace-sounds-and-music) · [Install and share a mod](#install-and-share-a-mod) · [Patch mods](#patch-mods) · [Settings and tweaks you can change](#settings-and-tweaks-you-can-change) · [Troubleshooting and FAQ](#troubleshooting-and-faq) · [What's changed for modders](#whats-changed-for-modders)

## Start here

**What you can mod today**

| I want to... | Use | Walkthrough |
|---|---|---|
| Repaint a monster or other textures (HD or just different) | a *texture pack* | [Retexture a monster](#retexture-a-monster) |
| Replace sounds, voices or music with my own | a *sound pack* | [Replace sounds and music](#replace-sounds-and-music) |
| Replace or add game files (data, scripts, sounds) with my own work | a *file mod* | [Install and share a mod](#install-and-share-a-mod) |
| Change a game file I must not share (a model, a bundle) | a *patch mod* | [Patch mods](#patch-mods) |
| Change how the game plays (rage meter, cheats, size, ...) | settings, no mod needed | [Settings and tweaks](#settings-and-tweaks-you-can-change) |

**What you need**
- Your own copy of the game (the Xbox disc image or an extracted folder). Your files are only ever read.
- The launcher and game from the [latest release](https://github.com/replayrevival/ste-recomp/releases/latest).
- For the texture and sound tools: **Python 3.9 or newer** (python.org, tick "Add python.exe to PATH") and the Pillow library: open a Command Prompt and type `pip install Pillow`.
- An image editor for textures (anything that saves PNG: GIMP, Krita, Photoshop, Paint.NET).

**Where mods go**
- Mods you install in the launcher live in the launcher's data folder: `data\mods\<id>\` (Mods tab > OPEN MODS FOLDER opens it). You do not need to copy anything by hand.
- The game writes `data\mods\last_report.txt` at every start: it lists what is ACTIVE or SKIPPED and why. Look there first when something does not show.

Technical detail (file formats, keys) lives in the game project's `docs\formats\` and is linked from here, not repeated.

## Retexture a monster

Five steps: **export** the original pictures, **edit** them, **import** them into a pack, **install** the pack, **share** it. About 15 minutes for the first monster.

**1. Export the monster's textures from your own game.** Open a Command Prompt in the folder that holds `import_texture_set.py` (it is in the game project's `tools\mods\`) and type (use your own paths):

```
python import_texture_set.py export "D:\Games\Godzilla Save the Earth" gigan_original --monster Gigan
```
- The game can be the folder or the `.iso` itself.
- `--monster ?` lists the monster names (Gigan, Godzilla2k, MechaGodzilla3, ...). `--monster` takes every picture of that monster: skin, bump map, weapons, effects and its HUD pictures. Use `--match Word1,Word2` for single textures and `--list` to only print names.
- You get PNG files named exactly like the game's textures, for example `Gigan_G2_512_C.png` (the **colour** skin) and `Gigan_G2_512_B.png` (the **bump map**; edit it only if you know what a bump map is). `export_report.csv` lists everything that was written.
- The game has one skin set per monster (there are no separate far-distance skins).

**2. Edit.** Copy the PNGs you want to change into a new folder (say `my_edits`) and paint over them. Rules:
- Keep the **file name** and keep the **same width/height ratio** (square stays square). Higher resolution is fine (up to 8192 pixels); the game builds the smaller copies itself.
- Keep transparency (alpha) where the original has it. Save as 8-bit PNG.
- Only put the pictures you changed in `my_edits`. The ones you leave out stay original.
- Only use pictures you made yourself or may share. The exported originals belong to the game's owners: never share them.

**3. Import into a texture pack.** One command (it reads your game once to find each texture, about 10 seconds):

```
python import_texture_set.py import my_edits "D:\Games\Godzilla Save the Earth" my_gigan_pack --id my-gigan --name "My Gigan retexture" --credits "your name, own work, CC0"
```
You get a folder `my_gigan_pack` with your pictures renamed to what the game expects, a `manifest.json` and `report.csv`. Open `report.csv`: every line should say `matched`. `unmatched` means the name is wrong (the tool suggests the closest names); `size-mismatch` means the shape differs from the original (it is still written, but check it looks right in the game).

**4. Install.** In the launcher open **Mods**, then **INSTALL FROM FOLDER...** and choose `my_gigan_pack` (or drag the folder onto the launcher window). It shows "Installed ... It is turned on; press PLAY to use it." Switch it ON or OFF in the list. Press **PLAY** and look at the monster.

![A texture pack installed in the Mods tab](screenshots/texmod_installed.png)

> Packs installed in the Mods tab are loaded by game version **0.2.2 or newer**. On older versions copy the pack folder into `texturepacks\` next to `ste-recomp.exe` and set `"enabled": true` in its `manifest.json`. To check that it worked, open `data\mods\last_report.txt`: it should say `texture pack <id>: ACTIVE, 1 file(s)`.

**5. Share.** Zip the `my_gigan_pack` folder (the zip holds your PNGs and `manifest.json`, never the game's originals) and put it on your own page. Say who made it, add a licence and credit anything you reused in `--credits`. The launcher users install it with **INSTALL FROM ZIP...**.

Details of the file name key, the pack format and the loader: game project `docs\formats\texturepack.md` and `texturepack_import.md`.

## Replace sounds and music

Replace the game's sound effects, the announcer and voices, or the music with your own. Five steps, like textures: **export** the originals as WAV files, **edit** them, **build** a pack, **install** it, **share** it.

> **Please read first: share only audio you made or have the rights to.** Packs that contain copyrighted music, or the game's own voice clips, are for your personal use only. Never put the game's original sounds into something you publish.

**What you can replace**
- **Effects**: hits, roars, explosions, menu clicks, ambient sounds.
- **The announcer and voices**: the lines like "VERSUS", "RAGE", the monsters' intro and win lines (English and Japanese voice banks).
- **Music**: the menu and fight tracks.

**1. Export the originals from your own game.** You need Python 3.9 or newer (no extra packages for sounds). Open a Command Prompt in the folder with `sound_pack.py` (it is in the game project's `tools\mods\`) and type, with your own paths:

```
python sound_pack.py export "D:\Games\Godzilla Save the Earth" my_sounds --match RAGE
```
- The game can be the folder or the `.iso`.
- `--match WORDS` exports every sound whose name contains one of the words (comma-separated). `--category effects`, `voices`, `music` or `monsters` exports a whole group. `--list` only prints the names.
- You get WAV files named like the game's sounds, for example `godzilla_voice_e_028_RAGE.wav` (the announcer's RAGE line) or one file per music track, plus `export_report.csv`.

**2. Edit in Audacity** (free, audacityteam.org) or any sound editor:
1. **File > Open** the WAV, make your changes (or record or import your own sound).
2. **File > Export > Export as WAV**. Choose **Signed 16-bit PCM** as the encoding.
3. **Keep the file name exactly as exported.** The name is how the tool knows which game sound you are replacing.

Rules for the files you put in your edits folder:
- WAV, **16-bit PCM**, **any sample rate**, **mono or stereo**, up to 60 seconds. Anything else (24-bit, floating point, compressed, longer) is refused with a plain message and the original sound stays.
- The length may differ from the original. Effects are mono in the game (a stereo file is mixed down to mono); music is stereo (a mono file is played on both sides).
- Put only the files you changed in the folder. The others stay original.
- **Loops:** to make a sound loop, **end its file name with `_loop`** (for example `shell_sound_001_ROLLOVER_loop.wav`). The whole sound then loops. A file without `_loop` plays once. (A loop point inside the WAV, written by editors such as Wavosaur, also works; Audacity does not write loop points, so use the file name.) Music tracks have no loop setting: the game restarts them when they end.

**3. Build the pack.**
```
python sound_pack.py import my_edits "D:\Games\Godzilla Save the Earth" my_sounds_pack --id my-sounds --name "My sounds" --credits "your name, own work, CC0"
```
You get a folder `my_sounds_pack` with your sounds, a `manifest.json` and `report.csv`. Open `report.csv`: each line says `matched`, or why not (`unmatched` = the name does not exist, `format-refused` = not 16-bit PCM WAV, a note says when the length or channels differ). The tool ends with the reminder about sharing only audio you have the rights to.

**4. Install.** In the launcher open **Mods**, then **INSTALL FROM FOLDER...** and choose `my_sounds_pack` (or drag the folder onto the launcher window). It shows up as a **Sound pack**. Switch it ON, press **PLAY**. The first start with a sound pack takes a few seconds longer while the game prepares the changed sounds; later starts are quick.

![A sound pack installed in the Mods tab](screenshots/sound_pack_installed.png)

> Needs game version **0.2.3 or newer**. Check `data\mods\last_report.txt`: it should say `sound pack <id>: ACTIVE, K file(s)`.

**5. Share.** Zip the `my_sounds_pack` folder and host it yourself. Say who made it, add a licence, credit what you reused. Remember the rule at the top: only audio you made or have the rights to. Details of the format: the game project's `docsormats\soundpack.md`.

## Install and share a mod

A *file mod* is a folder (or a zip of that folder) named after its id. A file `author.my-mod\ActionData\x` replaces the game's `ActionData\x` whenever the game reads it (same names and case as in the game folder).

```
author.my-mod\
  manifest.json          required
  ActionData\...         your own files, at game-relative paths
  ShellData\...
  README.md              ignored by the game; say what the mod does and who made it
```

**The manifest**
```json
{
  "api": 1,
  "id": "author.my-mod",
  "name": "My mod",
  "version": "1.0.0",
  "game": "STE-Xbox",
  "game_hashes": ["sha256:6966cc8a3a22d420d05362f24f3e68d7a462c481861189855f05583c0d6de500"],
  "authors": ["Your name"],
  "license": "CC-BY-4.0",
  "description": "What it does, in one line.",
  "credits": [{"name": "Thing", "role": "model", "source": "https://...", "license": "CC0"}]
}
```
- `id` is required: letters, digits, `.`, `-`, `_` only. It becomes the folder name.
- `game_hashes` is the SHA-256 of the retail `default.xbe`. If it does not match the player's game, the mod is skipped and marked. Always set it.
- `dependencies` (ids that must be on and load first) and `conflicts` (ids that must not be on) are checked by both the launcher and the game.
- `credits` lists every resource you did not make yourself, with its licence.

**Install.** Mods tab, **INSTALL FROM ZIP...** or **FROM FOLDER...** (or drag it onto the window). The launcher checks a zip first and refuses anything with paths that climb out of the mod folder, encrypted entries or oversized contents. A mod with the same id asks before replacing it; the old copy is moved aside.

![Choosing a mod zip in the launcher's browser](screenshots/mod_browser_zip.png)

A good mod is installed and switched on; one that is not safe or not valid is refused with a plain message and nothing is installed:

![Mod installed](screenshots/mod_install_zip_ok.png)
![Mod refused](screenshots/mod_install_zip_refused.png)

**Load order.** Later mods win when two change the same file. Set the order with EARLIER / LATER in the Mods tab. The launcher writes `order.json`; the game writes `last_report.txt`.

**Test it yourself.** Install on a clean copy, press PLAY, read `data\mods\last_report.txt` for `ACTIVE` lines, then remove the mod and start again.

**Share.** Zip the folder (manifest at the top or inside one folder), give it a licence in the manifest, credit what you reused, and host it yourself. You may not include game data, even "just a little". The launcher and tools are GPL-2.0-or-later; that does not apply to your mod's own files.

## Patch mods

Use a patch when your change is to a game file you must not share (a model, a bundle, a script that is mostly the original's). You publish only the difference. Format: game project `docs\formats\mod_patch.md`.

Add to the manifest (`"api": 2`):
```json
"patches": [{
  "target": "bundle\\MechaGodzilla3.cmf",
  "base_sha256": "sha256:<hash of the ORIGINAL file>",
  "result_sha256": "sha256:<hash of YOUR modified file>",
  "patch": "patches\\MechaGodzilla3.cmf.step",
  "size": 3412480
}]
```
One entry per patched file. If the player's file does not match `base_sha256` nothing is applied; if the result does not match `result_sha256` it is thrown away. Two mods can patch the same file only if the second one's `base_sha256` is the first one's `result_sha256`.

**Making a patch** with the project's `step_patch_tool` (build it with `cmake --build build --target step_patch_tool`):
```
step_patch_tool hash   <file>                              prints the SHA-256 (for the manifest)
step_patch_tool make   <original> <modified> <out.step>    writes the patch
step_patch_tool check  <original> <out.step> <modified>    applies it and compares
```
1. Copy your own original file out of your game folder and keep it untouched.
2. Make your modified file with your own tools.
3. `hash` both, `make` the patch, `check` it, and fill in the manifest.
4. Zip the mod folder. It must contain **no** original and no modified game file, only the patch and your own work.

Small edits give small patches (the tool keeps the common start and end and stores the middle; add `--raw` to skip compression). Install it like any mod. The launcher reads the player's own file (read only), checks its hash, builds the result into `mods\<id>\_built\` and checks that hash too; PLAY does this for you, **PREPARE PATCHES** does it by hand. Failures are per patch: "made for a different version of the game" (shows both hashes), "result did not match", "damaged patch". Patch mods need an unpacked game folder (they are not applied when you play straight from a disc image).

## Mods that are built on your PC (script mods)

Some mods change the game's scripts (Solo Destruction is one). The changed scripts are game data, so nobody may share them. Instead the mod ships only a **recipe**: a list of small edits (about 85 KB, no game data). The launcher builds the changed files from **your own** game files, checks each one against the hash the mod expects, and the game uses them.

**For players: Solo Destruction.**
1. Open the **Mods** tab. "Solo Destruction" is already there, switched **OFF**. It says "Needs build".
2. Switch it **ON** and press **PLAY**. The first time, the launcher builds the files (a few seconds, nothing is downloaded). Afterwards the Mods tab says "Built from your game files and ready".
3. In the game, the main menu has a **SOLO DESTRUCTION** row (it takes the place of the hidden System Link row). Pick your monster, press START, pick any monster for the COM (it is only needed to continue; it does not appear in the match), press X for Rules (ROUND LENGTH 1 to 10 minutes, or NO TIME LIMIT), pick a city and play. At 0:00 or when your monster is down, CHALLENGE RESULTS shows your points.
4. Switch the mod OFF and the menu and every mode are the original again.

It works with an unpacked game folder and with a disc image. If a build fails the launcher says why in plain words ("Your game file is not the one this mod was made for", "the script does not match what this tool expects, is this the original Xbox game?") and starts the game without that mod.

**Biollante.** The same way: "Biollante" is in the Mods tab, switched **OFF**. Switch it **ON** and press **PLAY**. The select screen gets a third row below the two original rows with Biollante in it, and she is playable as player 1, as player 2 (a second human on a second pad), as COM, in Versus, Melee, Action mode and Solo Destruction. Her models, animations, HUD and sounds come from your own game disc; the mod holds only a recipe. The grid is a little smaller so three rows fit. Switch the mod off and the select screen is the original again.

**Using several script mods at once.** Solo Destruction and Biollante can both be ON. The launcher builds all switched-on script mods in one go, in the order shown in the Mods tab, each on top of the earlier ones. If two mods change the same piece of a script, the launcher says so in plain words, leaves the later one out and starts the game without it; switch one of the two off. After you switch a mod on or off or change the order, the next PLAY rebuilds what is needed.

**For modders: a recipe mod.** A recipe mod is a normal mod folder (`"api": 2`) whose `patches` entries are built on the player's PC from their own files: `"format": "bsf-edit"` (a list of `old` / `new` script snippets in `edits\<name>.json`), `"format": "text-edit"` (the same idea for text files) or `"format": "builtin"` (a named builder in the tools, for example `builtin:bio_ui`), each with `base_sha256` and `result_sha256`. The launcher runs the project's tool for you (`iso_to_mod.py prepare-stack`, standard-library Python; the download carries its own small Python in `tools\python`, so nothing has to be installed). You can run it by hand too: `python -I iso_to_mod.py prepare-stack <mods folder> <game folder or .iso>` builds every switched-on recipe mod in load order, and `python -I iso_to_mod.py prepare <one mod folder> <game folder or .iso>` builds one mod alone. Each prints one line per file (`built`, `BASE MISMATCH`, `RESULT MISMATCH`, `CONFLICT ...`). When a mod is built on top of another mod's result, its hash cannot be in the manifest; the tool records it in `_built\_stack.json` and the game accepts it. Details: the game project's `docs/formats/mod_build_recipe.md`. Never publish the `_built` folder: it holds game data. If you edit `order.json` by hand, save it without a byte-order mark (the launcher writes it correctly).

## Make a mod from a modified disc image or folder

If you have edited a copy of the game (a disc image or an unpacked folder) and want to share it **without** sharing game files, make a patch mod from the difference. You need your own retail copy and the project's `iso_to_mod.py` (Python 3.9 or newer, nothing else):
```
python -I iso_to_mod.py diff <your modified image or folder> <your retail folder or image>
python -I iso_to_mod.py make <modified> <retail> <output mod folder> --id my.mod --name "My mod"
```
`diff` lists what changed. `make` writes a mod folder with only patches (the differences), never game files. Files that exist only in your modified copy cannot be shared this way unless you point them at a similar retail file with `--new-base NEW=RETAIL`; otherwise they are listed as "not expressible" and left out. Players install the mod from the Mods tab like any other; PLAY builds the patched files from their own game, including the zip packages some files live in, and checks every result. Only share what you made or have the rights to share. Details: the game project's `docs/formats/mod_patch.md`, section 8.

## Add a monster to the roster

A roster recipe adds a character to the select screen without shipping any game file. The select grid keeps the two original rows and gets one extra row below them; every extra monster is one entry in the slot table `SLOTS` of `tools\mods\bio_ui.py`, for example `{'name': 'biollante', 'row': 2, 'art': 'biollante_en', 'crop': (28, 34, 92)}`:
- `name`: the monster's script name,
- `art`: the portrait picture inside `ControlRoom.bdl` that the four grid icons are made from,
- `crop`: where the head is in that picture.

Up to 12 monsters fit in the extra row. The row is centred automatically and the cursor handles a row that is not full. With the mod off nothing changes.

You still need the monster's own data in the game files: model, animations, HUD bundle, sounds, its `attributes.xfg` entry and a `Setup<Name>AI` in `ConventionalGame.bsf`. Biollante's are on the retail disc; other monsters need their data added by another mod.

Steps:
1. Build the recipe: `python -I tools\mods\build_biollante.py <your game folder> <output mod folder>` (copy and edit this file for your monster).
2. Install the output folder from the **Mods** tab (**INSTALL FROM FOLDER...**), switch it on and press **PLAY**. The launcher builds the changed files from your own game and checks them.
3. If another script mod is on too and the two clash, the launcher names both; switch one off.

Known limits: when the select screen reopens, a monster in the extra row is not preselected, and the grid is smaller than the original. Reference: the game project's `docs/formats/roster_slots.md` and `docs/formats/mod_build_recipe.md`. Credit: the idea of a third select page was explored by ANILORAK on PS2 (research only; this is our own implementation, no code from him used).

## Photo mode

Photo mode pauses a fight so you can fly a free camera, change the field of view, roll the camera, hide the HUD and save full-resolution PNG screenshots. Open it from the in-game menu (**PHOTO MODE**), with the photo key (default **F8**) or, if you switch it on in the Controllers tab, by holding **Back** and **Y**. Close it with B or Esc. It works during a fight only. The settings (on/off, keys, camera speed, range, controller shortcut) are in the Controllers tab and listed in `ste_enhance.ini` under `[camera]`; they are in the settings tables below.

## Settings and tweaks you can change

These are the options in the launcher (Video and Enhancements tabs) and in the in-game menu (hold Back and Start, or F1). They are saved in `ste_video.ini` and `ste_enhance.ini` (in the launcher's `data\game_settings\`) and can also be set for one run with an environment variable. Off, or the first value, is the original game.

<!-- SETTINGS:BEGIN (generated by scripts/gen_modding_settings.py from config/settings.json: do not edit by hand) -->
### Video

| Option (launcher and in-game menu) | What it does | Values | Setting in the ini file | Default |
|---|---|---|---|---|
| Display | Which monitor the game opens on |  |  |  |
| Resolution | Native is your screen's own resolution |  |  |  |
| Window | Fullscreen (borderless) works best with variable refresh rate | Window, Borderless window, Fullscreen (borderless), Exclusive fullscreen (advanced) | `ste_video.ini` `[video]` `mode` | `window` |
| Gameplay view | Fit my screen shows more of the world. Menus always keep their original 4:3 shape | Original (4:3), Fit my screen | `ste_video.ini` `[video]` `aspect` | `4:3-pillar` |
| Render scale | Lower is faster, upscaled with FSR 1.0 | 100%, 85%, 75%, 67%, 50% | `ste_video.ini` `[video]` `render_scale` |  |
| Frame rate | The game keeps its original speed at any setting | 60, 30, Uncapped | `ste_video.ini` `[video]` `fps_cap` | `60` |
| V-Sync | On = no tearing (recommended). Off = lowest latency, may tear. | ON / OFF | `ste_video.ini` `[video]` `vsync` | `off` |
| Variable refresh rate | Needs FreeSync/G-Sync enabled on your monitor and graphics driver. | Auto, Off | `ste_video.ini` `[video]` `vrr` | `auto` |
| Draw distance | Stops distant objects from vanishing | Original, 2x, 4x, Unlimited | `ste_video.ini` `[video]` `draw_distance` | `1` |
| Field of view | Wider shows more of the arena | Original, Wider, Widest | `ste_video.ini` `[video]` `fov_scale` | `1` |
| Texture filtering | Sharper surfaces at an angle | Off, 2x, 4x, 8x, 16x | `ste_video.ini` `[video]` `anisotropy` | `0` |
| **SOUND AND POINTER** |  | | | |
| Volume | The game's overall volume. | 100%, 90%, 80%, 70%, 60%, 50%, 40%, 30%, 20%, 10%, 0% (mute) | `ste_video.ini` `[audio]` `master_volume` | `100` |
| Mouse pointer | Auto hides it after 3 seconds without movement. | Auto, Always hidden, Always shown | `ste_video.ini` `[window]` `hide_cursor` | `auto` |
| **HD ART** |  | | | |
| HD art | Sharper replacement pictures, made for this game. Needs the HD art files. (shown only when the game reports `hd_art`) | ON / OFF | `ste_video.ini` `[video]` `hd_art` | `0` |
| Sky | Replacement sky pictures. Only used while HD art is on. (shown only when the game reports `hd_art`) | On, Off | `ste_video.ini` `[video]` `hd_art_sky` | `1` |
| Ground | Replacement ground pictures. Only used while HD art is on. (shown only when the game reports `hd_art`) | On, Off | `ste_video.ini` `[video]` `hd_art_ground` | `1` |
| Roofs | Replacement roof pictures. Only used while HD art is on. (shown only when the game reports `hd_art`) | On, Off | `ste_video.ini` `[video]` `hd_art_roofs` | `1` |
| Billboards | Replacement billboard pictures. Only used while HD art is on. (shown only when the game reports `hd_art`) | On, Off | `ste_video.ini` `[video]` `hd_art_billboards` | `1` |

### Enhancements

| Option (launcher and in-game menu) | What it does | Values | Setting in the ini file | Default |
|---|---|---|---|---|
| **UNLOCKS** |  | | | |
| Unlock all monsters | The full 18-monster Versus grid. Not written to your save. | ON / OFF | `ste_enhance.ini` `[enhance]` `unlock_all_monsters` | `0` |
| Unlock all cities | Changes the city order. Not written to your save. | ON / OFF | `ste_enhance.ini` `[enhance]` `unlock_all_cities` | `0` |
| Unlock all challenges | Not written to your save. | ON / OFF | `ste_enhance.ini` `[enhance]` `unlock_all_challenges` | `0` |
| Unlock the gallery | All Gallery items. | ON / OFF | `ste_enhance.ini` `[enhance]` `unlock_gallery` | `0` |
| Unlock Final Wars items | All Final Wars gallery items. | ON / OFF | `ste_enhance.ini` `[enhance]` `unlock_final_wars` | `0` |
| Unlock Biollante | Needs the Biollante slot mod. | ON / OFF | `ste_enhance.ini` `[enhance]` `unlock_biollante` | `0` |
| **G:STEM-INSPIRED OPTIONS** | Inspired by G:STEM (our own code) - see Credits | | | |
| Monster size | Camera and hit boxes follow the size. | Original, 150%, 200%, 300% | `ste_enhance.ini` `[enhance]` `stem_rule_player_size` | `0` |
| Footstep camera shake | Big monsters shake the screen. | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_footstep_camera_shake` | `0` |
| Rage meter | How the rage meter fills. | Original, Damage taken, Damage dealt, Both, Over time, All | `ste_enhance.ini` `[enhance]` `stem_rule_rage_meter` | `0` |
| More traffic | Vehicles fill the city from the start. | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_more_ai_vehicles` | `0` |
| No juggle limit | Air combos are not capped. | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_no_juggle_limit` | `0` |
| No knockback scaling |  | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_no_knockback_scaling` | `0` |
| No minimum health |  | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_no_min_health` | `0` |
| Weapon-independent vertical knockback |  | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_weapon_vertical_knockback` | `0` |
| No results-screen dimming |  | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_no_results_dim` | `0` |
| Empty arena | Flat ground, no buildings. | ON / OFF | `ste_enhance.ini` `[enhance]` `stem_rule_empty_arena` | `0` |
| **GODZILLA ULTRA FEATURES** | Inspired by ANILORAK's Godzilla Ultra research (our own code) - see Credits | | | |
| **CHEATS** |  | | | |
| Player 1 invulnerable | Checked against the real game. | ON / OFF | `ste_enhance.ini` `[enhance]` `cheat_invulnerable_p1` | `0` |
| Computer player invulnerable |  | ON / OFF | `ste_enhance.ini` `[enhance]` `cheat_invulnerable_com` | `0` |
| Computer player super damage |  | ON / OFF | `ste_enhance.ini` `[enhance]` `cheat_super_damage_com` | `0` |
| Player 1 red |  | ON / OFF | `ste_enhance.ini` `[enhance]` `cheat_red_p1` | `0` |
| Computer player red |  | ON / OFF | `ste_enhance.ini` `[enhance]` `cheat_red_com` | `0` |
| **SAVES** |  | | | |
| Start with the game folder's saves | Off starts empty, like a new console. | ON / OFF | `ste_saves.ini` `[saves]` `seed_from_game_dir` | `0` |

### Presets

| Preset | What it does |
|---|---|
| ORIGINAL | Exactly how it looked on the console: original aspect, original draw distance, nothing extra. |
| MAX | Widescreen, full resolution, unlimited draw distance, 16x filtering, and everything unlocked. |
| LOW-END | 1080p output rendered at 720p and sharpened with FSR 1.0, for older PCs and handhelds. |

Every key can also be set for one run with the environment variable `STE_<SECTION>_<KEY>` (see the game project's `docs/LAUNCHER_INTERFACE.md`). Off, or the first value, is the original game.
<!-- SETTINGS:END -->

## Troubleshooting and FAQ

**My mod does not show in the Mods tab.** Is there a `manifest.json` at the top of the folder (or inside one top-level folder of the zip)? Is the `id` only letters, digits, `.`, `-`, `_`? The launcher shows a plain message when it refuses something.

**It shows "made for a different version of the game".** `game_hashes` does not contain the SHA-256 of the player's `default.xbe`. Check the hash with `step_patch_tool hash default.xbe`, or drop the field only for your own testing.

**The mod is installed but nothing changes in the game.** Is it switched ON? Press PLAY again (changes apply at the next start). Read `data\mods\last_report.txt`: it says ACTIVE or SKIPPED and why. A file mod replaces files by their path: names and upper/lower case must match the game folder exactly.

**Texture pack: `unmatched` in report.csv.** The PNG name is not a game texture name. Run the export again with `--list` and `--match` to see the real names; copy the exact name.

**Texture pack: `size-mismatch`.** The new picture has a different shape (width:height) than the original. It is still written, but it may look stretched.

**My texture is installed, but the monster looks the same.** Check `last_report.txt` and the game log line `[TEXPACK] <id> ... ACTIVE`. The monster may use another texture (try `--monster` for all of them), or the pack is switched off.

**`python` is not recognised.** Reinstall Python and tick "Add python.exe to PATH", or run `py import_texture_set.py ...`.

**`Pillow is missing`.** Type `pip install Pillow`.

**Can I put the original textures or files in my mod?** No. Never share game data.

**Does my mod work if the player runs the game from a disc image?** File mods and texture packs do. Patch mods need an unpacked game folder.

**My sound pack does not play.** Is it switched ON, and is the game 0.2.3 or newer? Read `last_report.txt`: refused files are listed there with the reason (usually not 16-bit PCM WAV). The first start rebuilds the changed sounds, so give it a few seconds.

**A replaced sound is too quiet or too loud.** Change the volume in your editor before exporting; the game plays the file as it is.

**Where do I report problems?** https://github.com/replayrevival/ste-recomp/issues

## What's changed for modders

- **0.4.1**: the Solo Destruction recipe also changes `ShellData\ControlRoom.bdl` (`builtin:solo_ui`, new tool `solo_ui.py`), so Solo Destruction and Biollante must be built with `prepare-stack`; the launcher does that for you. A changed bundled recipe replaces the installed one with the same id at the next launcher start (old copy to `_removed`).
- **0.4.0**: one release for everything since 0.2.3. New: Biollante as a roster recipe mod (`ste.biollante`, three build formats `bsf-edit`, `text-edit`, `builtin`); several recipe mods on at once (`iso_to_mod.py prepare-stack`, `_built\_stack.json`, `CONFLICT` lines); the guide section "Add a monster to the roster" (extra select row, `bio_ui.py`, `build_biollante.py`); edit `order.json` without a byte-order mark. Also in this release: Solo Destruction (a recipe mod that ships switched off), photo mode (`[camera]` settings, PHOTO MODE button in the in-game menu), the launcher builds script mods for you with an embedded Python in `tools\python`, "Mods that are built on your PC" and "Make a mod from a modified disc image or folder" walkthroughs. Recipe mods also work when you play from a disc image.
- **0.2.3**: sound packs (`sound_pack.py export` and `import`, mod type "Sound pack" in the Mods tab, loaded from the Mods folder with the on/off switch and order); a "Replace sounds and music" walkthrough.
- **0.2.2**: texture export (`import_texture_set.py export`) to get a monster's original textures as named PNGs, and `import` reads your game folder or `.iso` directly (no separate index step); texture packs installed in the Mods tab are loaded by the game from the Mods folder, with the Mods tab's ON/OFF and order (checked end to end: install in the launcher, PLAY, `last_report.txt` says ACTIVE); this guide restructured with a generated settings reference.
- **0.2.1**: no change for modders.
- **0.2.0**: play straight from a disc image (file mods and texture packs work; patch mods need an unpacked folder). The Enhancements settings that come from G:STEM are grouped as "G:STEM-inspired options" (credit in the launcher and the in-game menu).
- **0.1.7**: the texture pack import tool and texture pack loader are described in the game project's `docs\formats\texturepack*.md`.
- **0.1.4**: patch mods (api 2) and the Mods tab.
