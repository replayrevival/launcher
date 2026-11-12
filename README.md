# Replay Revival launcher

A small, controller-friendly launcher for the Replay Revival PC recompilations. It finds your own copy of the game, checks it, lets you pick video and enhancement options, maps your controllers, and starts the game with those settings. The first game it ships with is **Godzilla: Save the Earth** (Xbox).

![Video tab](docs/screenshots/video_tab.png)

- One launcher for every recomp: everything game-specific lives in `config/profile.json` (identity, game program, environment), `config/settings.json` (the options and presets) and `config/controls.json` (the controller actions).
- Your game files are only ever read. A disc image (.iso) is unpacked once into the launcher's own `data\game\` folder; an extracted folder is used in place.
- C++20, SDL3 + [RmlUi](https://github.com/mikke89/RmlUi) (OpenGL 3.3 behind `src/platform/UiBackend.h`, so another renderer can be added). The UI code takes a Context/Document and has no global window state, so it can later be hosted in-game as an overlay.

## Not included
**No game files, game data or game executables are in this repository, and none will be accepted.** You must own the game. The project is not affiliated with or endorsed by Toho, Atari or Pipeworks, or the owners of any other game it may support. Game names are used only to say which games the launcher works with.

## Build
See [BUILD.md](BUILD.md). Windows, Visual Studio / Build Tools, CMake; all libraries are vendored.

## Docs
- [docs/INPUT_MAPPING.md](docs/INPUT_MAPPING.md): controller mapping file the launcher writes and the game reads.
- [docs/INTERFACE_ASSUMPTIONS.md](docs/INTERFACE_ASSUMPTIONS.md): what the launcher expects from a game program.
- [docs/SCREEN_FLOW.md](docs/SCREEN_FLOW.md): the screens.
- [docs/MODS.md](docs/MODS.md): the Mods tab and install safety.
- [docs/OVERLAY.md](docs/OVERLAY.md): the planned in-game overlay.
- [CREDITS.md](CREDITS.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): dependencies, fonts and licences.

## Licence
GPL-2.0-or-later. `LICENSE` holds the GNU GPL version 2 text; you may also use any later version. Third-party code keeps its own licences (zlib, MIT, SIL OFL; FreeType under its GPLv2 option), see CREDITS.md.

## Network use
One optional, quiet update check once a day (can be switched off on the Overview tab). Details: `docs/NETWORK.md`.
