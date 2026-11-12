# Pinned third-party dependencies (see CREDITS.md). Each is fetched as a release
# archive and verified against a recorded SHA-256. For releases set
# -DLAUNCHER_VENDORED=ON and place the exact same sources in third_party/<name>/
# (no network access needed then).
include(FetchContent)

# Default: use third_party/ when it is there (a source checkout), download pinned archives otherwise.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL3/CMakeLists.txt")
    set(_launcher_vendored_default ON)
else()
    set(_launcher_vendored_default OFF)
endif()
option(LAUNCHER_VENDORED "Use sources from third_party/ instead of downloading" ${_launcher_vendored_default})
if(LAUNCHER_VENDORED)
    set(FETCHCONTENT_FULLY_DISCONNECTED ON)
endif()
set(LAUNCHER_DEPS_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "Download/extract cache for dependencies")
set(FETCHCONTENT_BASE_DIR "${LAUNCHER_DEPS_DIR}")
set(FETCHCONTENT_QUIET OFF)

function(launcher_dep name url hash)
    if(LAUNCHER_VENDORED)
        string(TOUPPER "${name}" _upper)  # FetchContent looks the override up by the upper-case name
        set(FETCHCONTENT_SOURCE_DIR_${_upper} "${CMAKE_SOURCE_DIR}/third_party/${name}" CACHE PATH "" FORCE)
    endif()
    FetchContent_Declare(${name} URL "${url}" URL_HASH SHA256=${hash} ${ARGN})
endfunction()

launcher_dep(SDL3
    https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
launcher_dep(RmlUi
    https://github.com/mikke89/RmlUi/archive/refs/tags/6.3.tar.gz
    d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede)
launcher_dep(Freetype
    https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz
    93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
    OVERRIDE_FIND_PACKAGE)
launcher_dep(nlohmann_json
    https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
# Reads mod .zip files (Mods tab).
launcher_dep(miniz
    https://github.com/richgel999/miniz/archive/refs/tags/3.1.2.tar.gz
    98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb
    SOURCE_SUBDIR _no_cmake_)  # we compile its four .c files ourselves (its CMakeLists is built for standalone use)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# SDL3 (static)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# FreeType (used under its GPLv2 option, see CREDITS.md); no optional codecs.
foreach(opt ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${opt} ON CACHE BOOL "" FORCE)
endforeach()
FetchContent_MakeAvailable(Freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# RmlUi core (static). We compile its SDL3 platform + GL3 renderer files ourselves.
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(RmlUi)

# nlohmann/json
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)


# miniz (zip reading for the Mods tab): static library from its sources, no generated export header needed.
FetchContent_MakeAvailable(miniz)
FetchContent_GetProperties(miniz SOURCE_DIR MINIZ_SOURCE_DIR)
file(WRITE "${CMAKE_BINARY_DIR}/miniz_gen/miniz_export.h" "#ifndef MINIZ_EXPORT\n#define MINIZ_EXPORT\n#endif\n")
add_library(miniz STATIC
    "${MINIZ_SOURCE_DIR}/miniz.c" "${MINIZ_SOURCE_DIR}/miniz_tdef.c"
    "${MINIZ_SOURCE_DIR}/miniz_tinfl.c" "${MINIZ_SOURCE_DIR}/miniz_zip.c")
target_include_directories(miniz PUBLIC "${MINIZ_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/miniz_gen")
FetchContent_GetProperties(RmlUi SOURCE_DIR RMLUI_SOURCE_DIR)
