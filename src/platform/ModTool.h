#pragma once
// Runs the game project's mod helper (tools/mods/iso_to_mod.py, standard-library Python) for mods that are built on the
// player's PC from their own files ("bsf-edit" recipes, "zip-of" zips). The launcher does not assume Python is installed:
// it looks for a bundled interpreter first (tools\python\python.exe next to the launcher), then for python.exe on PATH.
// Environment overrides for testing: LAUNCHER_PYTHON (interpreter), LAUNCHER_MOD_TOOL (script path).
#include <atomic>
#include <filesystem>

#include "core/mod_patch.h"

ExternalResult runModTool(const std::filesystem::path& exeDir, const std::filesystem::path& modDir, const std::filesystem::path& gameDir,
                          const std::atomic<bool>* cancel);
// `prepare-stack <mods root> <game> --enable ids`: builds all enabled recipe mods, each on top of the earlier ones.
ExternalResult runModToolStack(const std::filesystem::path& exeDir, const std::filesystem::path& modsRoot, const std::filesystem::path& gameDir,
                               const std::vector<std::string>& ids, const std::atomic<bool>* cancel);
