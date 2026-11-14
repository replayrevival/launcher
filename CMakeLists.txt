cmake_minimum_required(VERSION 3.25)
project(recomp_launcher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Shown in the launcher; scripts/package.ps1 passes -DLAUNCHER_VERSION_STRING=<version> for releases.
set(LAUNCHER_VERSION_STRING "0.1.3" CACHE STRING "Launcher version")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Deps)

# RmlUi's own SDL3 platform glue and OpenGL 3.3 renderer, compiled into the launcher.
# Everything else talks to them through src/platform/UiBackend.h so another renderer
# (SDL_GPU / Metal / Vulkan, goal 7) can be added later.
set(RMLUI_BACKEND_SRC
    "${RMLUI_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
    "${RMLUI_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp")

# launcher_ui: the window-free launcher (settings, input, mods, screens, navigation, decorators). No main(), no GL/D3D
# backend, no window creation: the planned in-game overlay (docs/OVERLAY.md) links this library and supplies its own
# renderer backend. The executable below adds main.cpp and the GL3 backend.
add_library(ste_ui_core STATIC
    src/core/config.cpp
    src/core/paths.cpp
    src/core/sha256.cpp
    src/core/xiso.cpp
    src/core/xdvdfs.cpp
    src/core/identify.cpp
    src/core/ini.cpp
    src/core/settings.cpp
    src/core/state.cpp
    src/core/input_map.cpp
    src/core/mods.cpp
    src/core/mod_patch.cpp
    src/core/launcher_model.cpp
    src/core/crash_report.cpp
    src/core/update_check.cpp
    src/core/file_browser.cpp
    src/core/display_modes.cpp
    src/platform/ExeVersion.cpp
    src/platform/png_decode.cpp
    src/ui/NavCore.cpp
    src/ui/ControllersUi.cpp
    src/ui/Overlay.cpp
    src/ui/ChamferDecorator.cpp
    src/ui/views/rows.cpp
    src/ui/views/mods.cpp
    src/ui/views/controllers.cpp
    src/ui/views/browser.cpp
)
target_include_directories(ste_ui_core PUBLIC src)
target_compile_definitions(ste_ui_core PUBLIC UNICODE _UNICODE PRIVATE LAUNCHER_VERSION="${LAUNCHER_VERSION_STRING}")
target_link_libraries(ste_ui_core PUBLIC RmlUi::RmlUi nlohmann_json::nlohmann_json miniz)
if(MSVC)
    target_compile_options(ste_ui_core PRIVATE /utf-8 /W3)
endif()
if(WIN32)
    target_link_libraries(ste_ui_core PUBLIC version)
endif()

# launcher_ui: the launcher screens and everything that needs SDL (events, pads, game process, monitors). The overlay does
# not use it: ste_overlay links ste_ui_core only, so a game that ships its own SDL gets no second copy.
add_library(launcher_ui STATIC
    src/platform/Displays.cpp
    src/platform/GameProcess.cpp
    src/platform/DriveSearch.cpp
    src/platform/UpdateFetch.cpp
    src/ui/App.cpp
    src/ui/AppMods.cpp
    src/ui/AppControllers.cpp
    src/ui/AppVideo.cpp
    src/ui/AppStopped.cpp
    src/ui/AppBrowser.cpp
    src/ui/Navigation.cpp
)
target_compile_definitions(launcher_ui PRIVATE LAUNCHER_VERSION="${LAUNCHER_VERSION_STRING}")
target_link_libraries(launcher_ui PUBLIC ste_ui_core SDL3::SDL3-static)
if(MSVC)
    target_compile_options(launcher_ui PRIVATE /utf-8 /W3)
endif()

add_executable(launcher
    src/main.cpp
    src/platform/UiBackend.cpp
    ${RMLUI_BACKEND_SRC}
)
target_include_directories(launcher PRIVATE "${RMLUI_SOURCE_DIR}/Backends")
target_compile_definitions(launcher PRIVATE RMLUI_SDL_VERSION_MAJOR=3 LAUNCHER_VERSION="${LAUNCHER_VERSION_STRING}")
target_link_libraries(launcher PRIVATE launcher_ui)
# Players get a Windows-subsystem program: no console window behind the launcher. Developer builds (scripts\build.cmd) turn
# LAUNCHER_CONSOLE on so test output on stderr stays visible and `launcher.exe` can be waited for in a shell.
option(LAUNCHER_CONSOLE "Build launcher.exe as a console program (developer builds)" OFF)
if(WIN32 AND NOT LAUNCHER_CONSOLE)
    set_target_properties(launcher PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(launcher PRIVATE /ENTRY:mainCRTStartup)  # keep int main(); no WinMain needed
endif()
if(MSVC)
    target_compile_options(launcher PRIVATE /utf-8 /W3)
endif()
# Dev layout: resources sit next to the exe.
add_custom_target(copy_resources ALL)
foreach(dir ui assets config)
    add_custom_command(TARGET copy_resources POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/${dir}" "$<TARGET_FILE_DIR:launcher>/${dir}")
endforeach()
# Dev builds run portable: settings and logs go to build/bin/data, not the user profile.
add_custom_command(TARGET copy_resources POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E touch "$<TARGET_FILE_DIR:launcher>/portable.txt")
add_dependencies(copy_resources launcher)

# Command-line check of the disc-image unpacker (list / unpack / compare), used by the release tests. Not shipped.
add_executable(xiso_unpack_tool EXCLUDE_FROM_ALL
    src/tools/xiso_unpack_tool.cpp src/core/xdvdfs.cpp src/core/identify.cpp src/core/sha256.cpp src/core/xiso.cpp)
target_compile_definitions(xiso_unpack_tool PRIVATE UNICODE _UNICODE)
target_include_directories(xiso_unpack_tool PRIVATE src)

# Patch-mod helper (make / check step1 patches, print SHA-256). Not part of the default build.
add_executable(step_patch_tool EXCLUDE_FROM_ALL src/tools/step_patch_tool.cpp)
target_link_libraries(step_patch_tool PRIVATE ste_ui_core)
if(MSVC)
    target_compile_options(step_patch_tool PRIVATE /utf-8)
endif()

# ---- In-game overlay (docs/OVERLAY.md), Windows / Direct3D 11 only. ste_overlay is part of the default build, so it can never be older than
# the headers of ste_ui_core (a stale ste_overlay.lib linked with a newer ste_ui_core.lib crashed in the game on 2026-11-14: different class layouts).
# The test host is built on request:
#   cmake --build build --target ste_overlay overlay_test_host
# ste_overlay: static library with include/ste_overlay_host.h as its API (launcher_ui + RmlUi's DX11 renderer on the game's device).
# overlay_test_host: a D3D11 window that draws tests/overlay/test_scene.png and the overlay on top of it. Not shipped.
if(WIN32)
    add_library(ste_overlay STATIC
        src/overlay_host/ste_overlay_host.cpp
        src/platform/UiBackendD3D11.cpp
        "${RMLUI_SOURCE_DIR}/Backends/RmlUi_Renderer_DX11.cpp")
    target_include_directories(ste_overlay PUBLIC include PRIVATE "${RMLUI_SOURCE_DIR}/Backends")
    target_link_libraries(ste_overlay PUBLIC ste_ui_core d3d11 dxgi d3dcompiler)
    if(MSVC)
        target_compile_options(ste_overlay PRIVATE /utf-8 /W3)
    endif()

    add_executable(overlay_test_host EXCLUDE_FROM_ALL tools/overlay_test_host.cpp)
    target_link_libraries(overlay_test_host PRIVATE ste_overlay)
    target_compile_definitions(overlay_test_host PRIVATE OVERLAY_TEST_SCENE="${CMAKE_SOURCE_DIR}/tests/overlay/test_scene.png")
    set_target_properties(overlay_test_host PROPERTIES WIN32_EXECUTABLE FALSE)
    if(MSVC)
        target_compile_options(overlay_test_host PRIVATE /utf-8 /W3)
    endif()
    # resources next to the exe, like the launcher's dev layout
    add_custom_command(TARGET overlay_test_host POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "$<TARGET_FILE_DIR:overlay_test_host>/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:overlay_test_host>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/config" "$<TARGET_FILE_DIR:overlay_test_host>/config")
endif()

# Stand-in game for testing the launcher's crash handling (tests/fake_game). Not shipped, not in the default build.
add_executable(fake_game EXCLUDE_FROM_ALL tests/fake_game/fake_game.cpp)
# Version comparison of the update check (a hotfix letter such as 0.1.3a counts as newer). Exit code 0 = pass.
add_executable(update_check_test EXCLUDE_FROM_ALL tests/update_check/update_check_test.cpp src/core/update_check.cpp)
target_include_directories(update_check_test PRIVATE src)
target_link_libraries(update_check_test PRIVATE nlohmann_json::nlohmann_json)
