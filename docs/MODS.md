# Mods in the launcher

The Mods tab manages the game's file-override mods (format: the game project's `docs/formats/mod_manifest.md`, api 1). The launcher never changes the game's files; it keeps mod folders in `<data>\mods\` and the game reads them through `STE_MODS_DIR` (set at every launch from `profile.json` `launch.mods_dir_env`).

```
<data>\mods\
  order.json              load order + enabled flags (written by the launcher, read by the game)
  last_report.txt         written by the game at every start (what is ACTIVE / SKIPPED and why)
  <id>\manifest.json ...  one folder per mod, named after the mod's id
  _removed\<id>-<stamp>\  removed mods (moved, never deleted; the game ignores folders starting with _ or .)
```

## What the tab does
- **Install from zip / folder**, or drag a zip or folder onto the window. The zip may have `manifest.json` at its root or inside one top-level folder. A mod with the id of an installed one asks "Replace it?" (the old copy goes to `_removed`).
- **ON/OFF** per mod, **EARLIER / LATER** for the load order (later wins a path conflict), **REMOVE** (asks first).
- **"Works with your game"** badge. The launcher checks what the manifest lets it check: `api` newer than 1, `game` other than `STE-Xbox`, `game_hashes` not containing the SHA-256 of the player's verified `default.xbe`, a missing / switched-off / wrongly ordered `dependencies` entry, an enabled mod listed in `conflicts`. The game applies the same rules at start and skips such a mod; the badge says so before the player presses PLAY. (Checked against the real game: its `last_report.txt` showed the same SKIPPED verdicts as the badges.)

## Install safety
- Zip entries are checked before anything is written: `..`, absolute or drive paths, empty parts, encrypted entries, more than 50 000 files or 4 GB are refused with a plain message. Nothing is left half-installed (files go to a staging folder `_installing_<stamp>` first and are renamed into place).
- Symlinks in a folder install are skipped. The source is only read.
- A mod id may only contain letters, digits, `.`, `-`, `_` (it becomes the folder name).
- Installing and removing run off the UI thread; they are refused while the game runs (files may be open).

## Not done yet
- **Patch-based mods** (apply a patch to a *copy* of the player's game files, verified by hash). The game project's `mod_manifest.md` (dated 2026-10-23) describes only file overrides; no patch format is documented yet. When the game session adds one, the launcher needs: a `patches` manifest section, a copy-on-write working folder under `<data>\patched_game\`, hash checks before and after, and `STE_GAME_PATH` pointing at that copy.
- Texture packs (`texturepacks\`, `STE_TEXPACKS_ENABLE`) have their own folder and are not part of this tab yet.

## Code
`src/core/mods.{h,cpp}` (scan, order, status, install, remove; no UI), `src/ui/AppMods.cpp` (tab and actions), miniz for zip reading (`cmake/Deps.cmake`).
