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

## Patch mods (manifest api 2; the game project's `docs/formats/mod_patch.md`, patch format `step1`)
A patch mod never contains game data: it ships binary patches that turn the player's own file into the modded file.
- **Prepare.** For every enabled mod that fits the game, the launcher reads the player's game file (read-only), checks its SHA-256 against `base_sha256`, applies the patch in memory, checks the result's SHA-256 (and `size` if given) and only then writes `mods\<id>\_built\<target>` (through a `.part` file and a rename). Nothing is written to the game folder.
- **When.** PLAY prepares what is missing or stale first (a "Preparing your mods" box), then starts the game. The Mods tab has a PREPARE PATCHES button for doing it by hand and shows `Patches: n of m prepared`.
- **Cache.** `_built\state.json` records base, result and patch hashes, the result size and the original's size and modification time, so later starts do not hash the game files again.
- **Failures** are shown per patch in plain language: base mismatch (both hashes, "made for a different version of the game"), result mismatch (output discarded), damaged or unsupported patch, missing target file, conflict. A failing mod builds nothing more, and PLAY offers PLAY ANYWAY (the game skips that mod). After a failure PLAY does not retry by itself in the same session; opening the Mods tab or PREPARE PATCHES re-arms it.
- **Chaining.** Two mods may patch the same file only when the later one's `base_sha256` is the earlier one's result; otherwise the later one is reported as a conflict.
- **Safety.** The decoder checks every size before it reads or writes, bounds the decompressed op stream, verifies the patch's CRC-32 and both hashes, and cannot touch memory outside its buffers. Tested with 60,000 randomly damaged patches (CRC fixed up): each was rejected or reproduced the exact expected hash; no crash. One file is held in memory at a time (limit 1.9 GB).
- **Tool.** `step_patch_tool` (`cmake --build build --target step_patch_tool`): `make <original> <modified> <out.step> [--raw]`, `check`, `hash`, `fuzz`. A simple starter encoder (shared start and end as COPY, the middle as INSERT), not the planned size-optimised one.
- **Game side, as of c304: not done.** The game's `last_report.txt` lists `_built\...` as new files of the mod instead of mapping them onto the target path, so a built file does not override the original yet. The launcher passes nothing extra; the game has to treat `_built\` as the mod's file tree (mod_patch.md, section 1, rule 2).

## Not done yet
- `bsdiff40` patches (they need bzip2).
- Texture packs (`texturepacks\`, `STE_TEXPACKS_ENABLE`) have their own folder and are not part of this tab yet.

## Code
`src/core/mods.{h,cpp}` (scan, order, status, install, remove), `src/core/mod_patch.{h,cpp}` (step1 decoder, prepare, cache), `src/ui/AppMods.cpp` (tab and actions), `src/tools/step_patch_tool.cpp`; miniz for zip reading and zlib (`cmake/Deps.cmake`). No UI in `core`.
