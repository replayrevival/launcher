#pragma once
// "Save crash report": a small zip a player can send when the game crashed. Contents: the newest minidump the game wrote, the
// game's log output, its ste_enhance.ini and ste_video.ini, and a short info text. No game files, nothing from the game's data.
// Text files have personal paths replaced (user name, game folder, launcher folders); a minidump is a memory snapshot and can
// not be cleaned, which the README in the zip says.
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct CrashEvidence {
    std::filesystem::path newestDump;  // empty = none from this run
    bool crashLine = false;            // "[CRASH]" appears in the game's log output
    bool any() const { return !newestDump.empty() || crashLine; }
};

// Looks for evidence that the run that just ended crashed: a .dmp in `dumpDir` written within `maxAgeSeconds`, or a [CRASH] line in a log.
CrashEvidence findCrashEvidence(const std::filesystem::path& dumpDir, const std::filesystem::path& outLog, const std::filesystem::path& errLog,
                                double maxAgeSeconds);

// Replaces each `from` (case-insensitive, both slash styles) by its `to`, then the user-profile folder name in any other path by <user>.
std::string redactPaths(const std::string& text, const std::vector<std::pair<std::string, std::string>>& replacements);

struct CrashReportInput {
    std::filesystem::path outZip;
    CrashEvidence evidence;
    std::filesystem::path outLog, errLog;  // the game's log files of the run
    std::filesystem::path settingsDir;     // folder with ste_enhance.ini / ste_video.ini
    std::string info;                      // launcher / game version, exit code, hardware (plain text)
    std::string supportText;               // where to send it, shown in the README
    std::vector<std::pair<std::string, std::string>> redactions;  // path -> replacement
};

// Writes the zip. False with a plain-language message in `err` when it could not be written.
bool writeCrashReport(const CrashReportInput& in, std::string& err);
