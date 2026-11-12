#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

// What the launcher remembers between runs (launcher_state.json in the data dir).
struct LauncherState {
    std::string gamePath;    // folder or disc image the user chose
    std::string gameKind;    // "folder" | "disc image"
    std::string gameTitle;   // from the game's certificate when it was verified
    std::string gameRegion;
    std::string sha256;      // of the game's main program file when verified
    uint64_t pathSize = 0;   // size and mtime of the chosen file, to notice changes
    int64_t pathMtime = 0;
    // Set when the game folder was unpacked from a disc image by the launcher: the image it came from (so picking it again reuses the folder).
    std::string sourceImage;
    uint64_t sourceSize = 0;
    int64_t sourceMtime = 0;

    bool vrrDefaultedOff = false;  // variable refresh was set to Off by the launcher's own default, not by the user
    int uiSize = 100;       // "UI size" percentage of the launcher (on top of the system's display scaling)
    bool devGameConsole = false;  // hidden developer toggle ("dev_game_console" in launcher_state.json): the game opens its own console
    bool updateCheck = true;      // look for a newer launcher release now and then (a quiet request to GitHub)
    int64_t lastUpdateCheck = 0;  // unix seconds of the last request
    std::string latestTag, latestUrl;  // newest release seen, kept so the note shows even when offline later
    int displayChoice = -1;  // monitor the user picked on the Video tab (-1 = never chose: use the one the launcher is on)

    bool load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file, std::string& err) const;
    void clearGame();
};

// Size and modification time of `p` (0 when unavailable); used to detect a changed file.
void statPath(const std::filesystem::path& p, uint64_t& size, int64_t& mtime);
