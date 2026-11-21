#pragma once
#include <filesystem>
#include <string>
#include <vector>

// Mods for the game's file-override loader (format: the game project's docs/formats/mod_manifest.md, api 1).
//   <root>\order.json            load order + enable flags (written by the launcher, read by the game)
//   <root>\<folder>\manifest.json
//   <root>\_removed\             removed mods are moved here (the game ignores folders starting with _ or .)
// The game's own files are never touched; the game redirects reads to the mod folders.
// No UI and no window code in here.

// One entry of "patches" in an api 2 manifest (hashes are lower-case hex without the "sha256:" prefix).
struct ModPatchEntry {
    std::string target;       // game-relative path, '/' separated
    std::string baseSha256, resultSha256, patchRel, patchSha256, format = "step1";
    unsigned long long size = 0;  // expected size of the result, 0 = not given
};

struct ModInfo {
    std::vector<ModPatchEntry> patches;
    std::string id, name, version, description, game, profile, license;
    std::vector<std::string> authors, gameHashes, dependencies, optionalDependencies, conflicts;
    int priority = 100;
    int api = 1;
    bool enabledByManifest = true;
    std::string folder;          // folder name inside the mods root
    std::filesystem::path dir;
    std::string problem;         // manifest unreadable / invalid: the mod cannot be used
    bool enabled = true;         // after order.json
    const std::string& title() const { return name.empty() ? id : name; }
};

enum class ModFit { Ok, BadManifest, NewerApi, WrongGame, WrongVersion, MissingDependency, DependencyOff, Conflict };

struct ModStatus {
    ModFit fit = ModFit::Ok;
    std::string text;            // plain language, shown under the mod
    bool problem() const { return fit != ModFit::Ok; }
};

struct ModInstallResult {
    enum Kind { Ok, Failed, AlreadyInstalled } kind = Failed;
    std::string id, name;        // the mod that was (or would be) installed
    std::string error;           // plain language when not Ok
    std::filesystem::path sourceRoot;  // for AlreadyInstalled: lets the caller retry with replace=true
};

class ModStore {
public:
    static constexpr int kApi = 1;             // api written to order.json
    static constexpr int kMaxManifestApi = 2;  // newest manifest api understood (2 = patch mods, see mod_patch.h)

    void setRoot(const std::filesystem::path& root) { root_ = root; }
    const std::filesystem::path& root() const { return root_; }

    // Scans the folders and order.json. Mods are kept in load order (the order the game will use).
    void reload();
    const std::vector<ModInfo>& mods() const { return mods_; }
    const ModInfo* find(const std::string& id) const;

    bool setEnabled(const std::string& id, bool on);
    bool move(const std::string& id, int delta);        // -1 = earlier, +1 = later
    bool save(std::string& err) const;                  // order.json, atomic; other keys in the file are kept

    // How well a mod fits the player's game. `xbeSha256` = SHA-256 (hex) of the verified default.xbe.
    ModStatus status(const ModInfo& m, const std::string& xbeSha256, const std::string& expectedGame) const;

    // What can be installed? Looks at a .zip file or a folder; fills the id/name if it is a mod.
    static bool looksLikeMod(const std::filesystem::path& p);

    // Both copy into the mods root (the original is only read). With replace=true an installed mod of the
    // same id is moved to _removed first. A bad zip (unsafe paths, too big, damaged) is refused with a plain message.
    ModInstallResult installZip(const std::filesystem::path& zip, bool replace);
    ModInstallResult installFolder(const std::filesystem::path& folder, bool replace);

    // Mods that ship inside the download (<launcher folder>undled_mods\<folder>): each is installed once, switched OFF, and a
    // marker in <root>\_seeded\ remembers it, so a mod the player removed does not come back.
    void seedBundled(const std::filesystem::path& bundledDir);

    // Moves the mod's folder to _removed\<folder>-<stamp>. Nothing is deleted.
    bool remove(const std::string& id, std::string& err);

private:
    bool readManifest(const std::filesystem::path& manifest, ModInfo& out, std::string& err) const;
    ModInstallResult finishInstall(const std::filesystem::path& staged, const ModInfo& info, bool replace);
    std::filesystem::path root_;
    std::vector<ModInfo> mods_;
};

// Limits for one install (zip or folder).
constexpr unsigned long long kModMaxBytes = 4ull << 30;   // 4 GB of files
constexpr unsigned kModMaxFiles = 50000;
