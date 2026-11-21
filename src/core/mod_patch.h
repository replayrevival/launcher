#pragma once
// Patch-based mods (the game project's docs/formats/mod_patch.md, manifest api 2, patch format "step1").
// A mod ships binary patches, never game data. "Preparing" a mod reads the player's own game file (read-only),
// checks its SHA-256 against the manifest, applies the patch in memory, checks the result's SHA-256 and writes it to
// <mod folder>\_built\<target>. The game then sees the built file as part of the mod. The original is never written.
// No UI and no window code in here.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "core/mods.h"

constexpr const char* kPatchToolName = "launcher-step 1";

enum class PatchState {
    NotBuilt,        // nothing built yet (or the inputs changed)
    Ready,           // built, and the cached state matches
    Built,           // just built by this run
    BaseMismatch,    // the player's file is not the one the patch was made for
    ResultMismatch,  // the patched file did not hash to the expected value; output discarded
    BadPatch,        // damaged, wrong or unsupported patch file
    MissingTarget,   // the game has no such file
    Conflict,        // another mod patches the same file and this one does not build on its result
    Skipped          // not tried (the mod is off, does not fit the game, or an earlier patch of the mod failed)
};

struct ModPatchReport {
    std::string modId;
    std::string target;
    PatchState state = PatchState::NotBuilt;
    std::string detail;  // plain language; hashes are short forms
    bool ok() const { return state == PatchState::Ready || state == PatchState::Built; }
};

// Decodes one step1 patch (whole file in memory) against the original bytes. Checks every size before it reads or
// writes. Does not check the manifest; `expectBase` / `expectResult` (hex, no prefix) are compared with the header.
// On success `out` holds the patched file and its SHA-256 matches the header's result hash.
bool applyStep1(const std::vector<uint8_t>& patch, const std::vector<uint8_t>& original, const std::string& expectBase,
                const std::string& expectResult, std::vector<uint8_t>& out, PatchState& failState, std::string& err);

// Test / modder helper: a simple encoder (shared prefix and suffix as COPY, the middle as ADD or INSERT).
std::vector<uint8_t> encodeStep1Simple(const std::vector<uint8_t>& original, const std::vector<uint8_t>& result, bool compress);

std::string sha256OfFile(const std::filesystem::path& p, std::string& err);   // hex, empty on error
std::string sha256OfBytes(const std::vector<uint8_t>& b);

// What is built and current, from <mod>\_built\state.json (no hashing of game files). One entry per patch of the mod.
std::vector<ModPatchReport> quickPatchStates(const ModInfo& mod, const std::filesystem::path& gameDir, const ModStore* store = nullptr);

// Mods whose patches are "bsf-edit" (a recipe built from the player's own scripts) or "zip-of" (a zip rebuilt from a patched
// member) are built by the game project's tool (iso_to_mod.py prepare), never by this launcher. The runner starts it:
// `prepare <mod folder> <game folder>`, exit 0 = all built, 2 = a file failed, 1 = usage; one line per file on stdout.
struct ExternalResult {
    bool started = false;
    std::string startError;  // plain language when the tool could not be started
    int exitCode = -1;
    std::string output;
};
using ExternalRunner = std::function<ExternalResult(const std::filesystem::path& modDir, const std::filesystem::path& gameDir)>;
// Recipe mods (bsf-edit, text-edit, builtin) are built together, on top of each other in load order:
// `prepare-stack <mods root> <game folder> --enable id,id`; lines are `<mod id>: <target>: built...` or `CONFLICT <mod id>: <target>: ...`.
using StackRunner = std::function<ExternalResult(const std::filesystem::path& modsRoot, const std::filesystem::path& gameDir, const std::vector<std::string>& ids)>;
// Recipe mods (bsf-edit, text-edit, builtin) are built together, on top of each other in load order:
// `prepare-stack <mods root> <game folder> --enable id,id`; lines are `<mod id>: <target>: built...` or `CONFLICT <mod id>: <target>: ...`.
using StackRunner = std::function<ExternalResult(const std::filesystem::path& modsRoot, const std::filesystem::path& gameDir, const std::vector<std::string>& ids)>;
bool isExternalPatch(const ModPatchEntry& p);
bool modNeedsExternalTool(const ModInfo& m);

struct PatchRunResult {
    std::vector<ModPatchReport> reports;  // every patch of every enabled patch mod, in load order
    int built = 0, ready = 0, failed = 0;
};
// Builds what is missing or stale for all enabled mods that fit the game (`fits(mod)` decides). Mods are handled in
// load order; two mods may patch the same file only when the later one's base is the earlier one's result.
// `cancel` may be null; it is checked between files.
PatchRunResult preparePatches(const ModStore& store, const std::filesystem::path& gameDir, const std::function<bool(const ModInfo&)>& fits,
                              const std::atomic<bool>* cancel, const ExternalRunner& external = {}, const StackRunner& stack = {});
