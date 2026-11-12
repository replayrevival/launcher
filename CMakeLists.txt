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
add_library(launcher_ui STATIC
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
    src/core/display_modes.cpp
    src/platform/Displays.cpp
    src/platform/GameProcess.cpp
    src/platform/DriveSearch.cpp
    src/platform/ExeVersion.cpp
    src/ui/App.cpp
    src/ui/AppMods.cpp
    src/ui/AppControllers.cpp
    src/ui/AppVideo.cpp
    src/ui/AppStopped.cpp
    src/ui/ChamferDecorator.cpp
    src/ui/Navigation.cpp
    src/ui/views/rows.cpp
)
target_include_directories(launcher_ui PUBLIC src)
target_compile_definitions(launcher_ui PUBLIC UNICODE _UNICODE PRIVATE LAUNCHER_VERSION="${LAUNCHER_VERSION_STRING}")
target_link_libraries(launcher_ui PUBLIC RmlUi::RmlUi SDL3::SDL3-static nlohmann_json::nlohmann_json miniz)
if(MSVC)
    target_compile_options(launcher_ui PRIVATE /utf-8 /W3)
endif()
if(WIN32)
    target_link_libraries(launcher_ui PUBLIC version)
endif()

add_executable(launcher
    src/main.cpp
    src/platform/UiBackend.cpp
    ${RMLUI_BACKEND_SRC}
)
target_include_directories(launcher PRIVATE "${RMLUI_SOURCE_DIR}/Backends")
target_compile_definitions(launcher PRIVATE RMLUI_SDL_VERSION_MAJOR=3 LAUNCHER_VERSION="${LAUNCHER_VERSION_STRING}")
target_link_libraries(launcher PRIVATE launcher_ui)
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
target_link_libraries(step_patch_tool PRIVATE launcher_ui)
if(MSVC)
    target_compile_options(step_patch_tool PRIVATE /utf-8)
endif()
