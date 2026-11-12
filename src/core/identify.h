#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class IdStatus {
    Ok,            // verified: matches a known-good copy
    WrongVersion,  // the right game, but not a version we support
    WrongGame,     // a different Xbox game
    OtherPlatform, // PS2 / Wii / GameCube image
    NotAGame,      // not a disc image or game folder
    NoXbe,         // looks like a game folder/disc but has no default.xbe
    NotFound,
    CantRead,
};

struct IdResult {
    IdStatus status = IdStatus::NotFound;
    std::string kind;     // "folder" or "disc image"
    std::string title;    // from the XBE certificate
    std::string region;   // e.g. "NTSC (USA)"
    std::string sha256;   // of default.xbe
    std::string message;  // plain-language summary for the UI
};

struct IdExpect {
    std::vector<std::string> sha256;  // known-good default.xbe hashes
    std::string titleHint;            // part of the expected title, for "right game, wrong version"
};

// Reads (never writes) the file or folder and works out what it is.
IdResult identifyGame(const std::filesystem::path& path, const IdExpect& expect);

// UTF-8 <-> path helpers (std::filesystem::u8path is deprecated in C++20).
std::filesystem::path pathFromUtf8(const std::string& s);
std::string pathToUtf8(const std::filesystem::path& p);
