#pragma once
#include <filesystem>

// Portable mode: if portable.txt sits next to the launcher exe, all launcher state
// lives in <exe dir>/data; otherwise in `userPrefDir` (the per-user dir the platform
// layer provides, e.g. SDL_GetPrefPath).
std::filesystem::path dataDir(const std::filesystem::path& exeDir, const std::filesystem::path& userPrefDir);
bool isPortable(const std::filesystem::path& exeDir);
