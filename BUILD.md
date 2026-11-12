# Building the launcher

Windows 10/11, 64-bit. Other platforms are not tested yet (the code is SDL3 + OpenGL 3.3, so a port should be mostly build scripts).

## You need
- Visual Studio 2022 or newer, or the Build Tools, with the **Desktop development with C++** workload (MSVC x64, Windows SDK).
- CMake 3.25 or newer (on `PATH`, or set `CMAKE` to the full path of `cmake.exe`).
- Nothing else. SDL3, RmlUi, FreeType, nlohmann/json and miniz are vendored in `third_party\` and the build needs no network.

## Build
```
git clone <this repository>
cd launcher
scripts\build.cmd
```
The result is `build\bin\launcher.exe`. The build also copies `ui\`, `assets\` and `config\` next to it and creates `build\bin\portable.txt`, so a dev build keeps its settings, saved game and logs in `build\bin\data\` instead of your user profile.

Variables for `scripts\build.cmd`: `CMAKE` (path of cmake.exe), `BUILD_DIR` (default `build`). Extra arguments go to the CMake configure step.

## Console builds
By default `launcher.exe` is a Windows program without a console window. For development, pass `-DLAUNCHER_CONSOLE=ON` to `scripts\build.cmd` to get a console program: test output on stderr stays visible and a shell waits for the exe.

## Try it
```
build\bin\launcher.exe
```
Pick your own copy of the game (an .iso or an extracted folder). To start a game program that is not next to the launcher while developing, set `LAUNCHER_GAME_EXE` to its path first.

Handy test flags (used for the screenshots in `docs\screenshots`): `--script up,down,confirm,...` to play back key presses, `--shot out.png` to save a screenshot and quit, `--hold <seconds>`, `--size WxH`, `--fresh` (ignore the remembered game), `--fake-displays tests\displays\fake_21x9.json` (pretend to have other monitors), `--content-scale 1.5`.

## In-game overlay library (optional, Windows)
Not part of the default build. `cmake --build build --target ste_overlay overlay_test_host` builds the static library behind `include/ste_overlay_host.h` (the in-game overlay for a game that uses Direct3D 11, see `docs\OVERLAY.md` and `docs\OVERLAY_FOR_GAME_SESSION.md`) and a small test window that draws it over a test picture. Run `overlay_test_host.exe` from `build\bin` (F1 opens the overlay).

## Pinned dependencies
`cmake\Deps.cmake` pins every dependency by URL and SHA-256; `third_party\README.md` lists what is vendored. If `third_party\` is missing, CMake downloads the pinned archives instead (needs network). Force either way with `-DLAUNCHER_VENDORED=ON|OFF`.

## Releases
`powershell -File scripts\package.ps1 -Version 0.1.3 -GameExe <path to the game's ste-recomp.exe> -Out <output folder>` builds a distribution zip, a manifest with SHA-256 of every file, and a changelog (the user-facing text comes from `packaging\changes\<version>.txt`). Run `Get-Help .\scripts\package.ps1 -Full` for the options and the checks it makes.
