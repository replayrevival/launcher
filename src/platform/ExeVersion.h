#pragma once
#include <filesystem>
#include <string>

// Version string embedded in the game program ("" when there is none or it cannot be read).
// Windows: the VERSIONINFO ProductVersion. Other platforms: not implemented yet.
std::string readExeVersion(const std::filesystem::path& exe);
