#pragma once
#include <map>
#include <string>
#include <vector>

// Per-game profile (config/profile.json). Nothing game-specific lives in code.
struct Profile {
    std::string brand = "[BRAND]";
    std::string gameId;
    std::string gameTitle;
    std::string exe;                          // game program name, next to the launcher unless overridden
    std::string gamePathEnv = "STE_GAME_PATH";  // env var that carries the user's game folder / disc image
    std::vector<std::string> xbeSha256;       // known-good hashes of the game's main program file
    std::string titleHint;                    // part of the game's title, to recognise "right game, wrong version"
    std::map<std::string, std::string> env;   // baseline environment for the game
    std::map<std::string, std::string> iniEnv;  // ini file id ("video") -> env var that points the game at it
    std::string modsDir = "mods";   // folder name inside the launcher data folder
    std::string modsDirEnv;         // env var that points the game at it; empty = the game has no mods folder setting
    std::string expectedGameId = "STE-Xbox";  // the "game" a mod manifest must name (when it names one)
    std::string buttonPromptsEnv;  // env var that carries the "Button prompts" choice; empty = the game has no such setting
    std::string inputMapEnv = "STE_INPUT_MAP";  // env var that carries the path of input_mapping.json
    std::string inputMapFile = "input_mapping.json";
    bool inputMapSupported = false;             // does the game build read the mapping file yet?
    int vsyncVrrFixBuild = 0;                   // first game build with the V-Sync/VRR start-up fix (0 = none known)
};

// Loads the profile; returns false on error and fills `err` in plain language.
bool loadProfile(const std::string& path, Profile& out, std::string& err);
