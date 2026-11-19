// "The game stopped" box: what happened (told apart: closed by itself / failed to start / crashed),
// what to try next, and the details to send to support.
#include <SDL3/SDL.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "core/crash_report.h"
#include "core/identify.h"
#include "ui/App.h"

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

namespace {

// Windows crash codes people actually meet, in plain words.
const char* windowsCrashName(unsigned c) {
    switch (c) {
        case 0xC0000005u: return "access violation";
        case 0xC000001Du: return "illegal instruction";
        case 0xC0000094u: return "integer divide by zero";
        case 0xC00000FDu: return "stack overflow";
        case 0xC0000135u: return "a required DLL was not found";
        case 0xC0000142u: return "a DLL failed to start";
        case 0xC000007Bu: return "wrong program or DLL type";
        case 0xC0000409u: return "the program stopped itself after detecting damage";
        case 0xC0000374u: return "heap corruption";
        default: return nullptr;
    }
}

std::string hex(unsigned v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

}  // namespace

float App::uiScale() const { return float(state_.uiSize) / 100.f; }

std::string App::supportDetails(int code, double secs) const {
    std::ostringstream d;
    d << "Launcher " << LAUNCHER_VERSION << " | game " << (gameVersion_.empty() ? "build unknown" : gameVersion_) << "\n";
    d << "Exit code: " << code << " (" << hex(unsigned(code)) << "), ran " << int(secs) << " s\n";
    d << "Start: " << (lastOriginal_ ? "original settings (one-off)" : "saved settings") << (lastBorderless_ ? ", borderless (one-off)" : "")
      << ", preset " << (lastPlayedPreset_.empty() ? "Custom" : lastPlayedPreset_) << "\n";
    if (!disp_.displays.empty()) {
        const DisplayInfo& dp = disp_.displays[size_t(std::min<int>(selDisplay_, int(disp_.displays.size()) - 1))];
        d << "Display " << selDisplay_ << ": " << dp.name << ", native " << dp.desktop.w << "x" << dp.desktop.h << " @ " << int(dp.desktop.hz)
          << " Hz, OS scale " << int(dp.scale * 100.f) << "%, " << disp_.displays.size() << " monitor(s)\n";
    }
    d << "Graphics: " << (gpuInfo_.empty() ? "unknown" : gpuInfo_) << "\n";
    d << "Video settings used (" << pathToUtf8(lastVideoIni_) << "):\n";
    {
        std::ifstream f(lastVideoIni_);
        std::string line;
        bool any = false;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == ';') continue;
            d << "  " << line << "\n";
            any = true;
        }
        if (!any) d << "  (none: the game's own defaults)\n";
    }
    d << "Log files: " << pathToUtf8(game_.errLog()) << "\n";
    {
        // The newest crash dump the game wrote (STE_DUMP_DIR), if any.
        std::error_code ec;
        std::filesystem::path newest;
        std::filesystem::file_time_type best{};
        for (auto& e : std::filesystem::directory_iterator(dumpDir(), ec))
            if (e.path().extension() == ".dmp" && (newest.empty() || e.last_write_time() > best)) { newest = e.path(); best = e.last_write_time(); }
        d << "Crash dump folder: " << pathToUtf8(dumpDir()) << (newest.empty() ? " (no dump)\n" : "\nNewest dump: " + pathToUtf8(newest) + "\n");
    }
    d << "--- last lines of the game's error log ---\n" << game_.errorTail(30);
    d << "--- last lines of the game's normal log ---\n" << game_.outputTail(20);
    return d.str();
}

void App::onGameExit(int code, double secs) {
    lastExitCode_ = code;
    lastRunSecs_ = secs;
    const unsigned ucode = unsigned(code);
    std::string title, body;
    const std::string after = " after " + std::to_string(int(secs)) + " seconds";

    if (code == 0) {
        title = "The game closed right after starting";
        body = "The game ended on its own" + after + " and reported no error (exit code 0). It did not crash. "
               "If you did not close it yourself, you can try again or use the options below.";
    } else if (ucode == 0xFFFFFFFFu) {
        title = "The game was ended from outside";
        body = "The game was stopped by something other than itself" + after + ", for example Task Manager, another program, or "
               "Windows shutting it down (exit code -1). This is not a crash report from the game.";
    } else if (ucode >= 0xC0000000u) {
        title = "The game crashed";
        const char* n = windowsCrashName(ucode);
        body = "Windows stopped the game" + after + " (" + hex(ucode) + (n ? std::string(": ") + n : std::string()) + ").";
    } else if (code == 1) {
        title = "The game could not finish starting";
        body = "The game reported a start-up problem (exit code 1)" + after + ". The details below are its own last messages.";
    } else if (code == 2) {
        title = "The game did not get a game folder";
        body = "The game stopped with exit code 2: it was not told where your game is, or the start option was not accepted. "
               "The launcher normally passes your game folder itself, so please press Copy details and send it with your bug report. "
               "Choosing your game again from the Overview tab may help.";
    } else if (code == 3) {
        const bool img = playingFromImage();
        title = img ? "The game could not use your disc image" : "The game could not use your game folder";
        body = img ? "The game's own check of your disc image failed (exit code 3). It says what it did not like in the details below (for example that the file is not an Xbox disc image, or has no game program in it). Choose your game again from the Overview tab, or unpack the image into a folder yourself and choose that folder."
                   : "The game's own check of your game folder failed (exit code 3). Choose your game again from the Overview tab.";
    } else if (code == 6) {
        title = "The game hit an internal error";
        body = "The game stopped because of an internal error in its own code (exit code 6)" + after +
               ". This is not caused by your settings. Please send the details to support.";
    } else if (code == 10) {
        title = "Your graphics driver stopped";
        body = "The graphics device was removed or reset" + after + " (exit code 10): a driver reset or crash, or a GPU that was "
               "unplugged. Updating the graphics driver may help.";
    } else {
        title = "The game stopped";
        body = "The game stopped with exit code " + std::to_string(code) + " (" + hex(ucode) + ")" + after + ".";
    }
    body += "\nYour saved settings and game file were not changed.";

    // Retrying with other settings only makes sense when settings could be the cause (not for a bad folder,
    // a bad command line or an internal error of the game's code).
    const bool settingsCouldMatter = code != 2 && code != 3 && code != 6;
    std::vector<ModalButton> buttons;
    if (settingsCouldMatter && !lastOriginal_) buttons.push_back({"TRY AGAIN WITH ORIGINAL SETTINGS", "modal-retry-original", ""});
    if (settingsCouldMatter && videoMode() == "exclusive" && !lastBorderless_ && !lastOriginal_)
        buttons.push_back({"TRY BORDERLESS INSTEAD", "modal-retry-borderless", ""});
    // A crash that left a dump or a [CRASH] line: offer a report the player can send (nothing is sent by the launcher).
    if (code != 0 && findCrashEvidence(dumpDir(), game_.outLog(), game_.errLog(), secs + 120.0).any())
        buttons.push_back({"SAVE CRASH REPORT", "modal-save-report", ""});
    buttons.push_back({"OPEN LOG FOLDER", "modal-open-logs", ""});

    std::string shown = game_.errorTail(5);
    if (shown.empty()) shown = game_.outputTail(4);
    showModalEx(title, body, shown, supportDetails(code, secs), buttons);
}

// Zips the newest crash dump, the game's logs and ini files (paths cleaned) to the desktop and says where to send it.
void App::saveCrashReport() {
    CrashReportInput in;
    in.evidence = findCrashEvidence(dumpDir(), game_.outLog(), game_.errLog(), lastRunSecs_ + 120.0);
    in.outLog = game_.outLog();
    in.errLog = game_.errLog();
    in.settingsDir = settingsDir();
    in.info = supportDetails(lastExitCode_, lastRunSecs_);
    in.supportText = "Please send this zip to the people working on the game: " + profile_.supportUrl +
                     " (open a new issue there and attach the zip), or give it to whoever gave you the launcher.";
    auto add = [&](const std::string& from, const std::string& to) { if (!from.empty()) in.redactions.push_back({from, to}); };
    add(state_.gamePath, "<game folder>");
    add(pathToUtf8(paths_.dataDir), "<launcher data>");
    add(pathToUtf8(paths_.exeDir), "<launcher folder>");

    std::string desktop;
    if (const char* o = std::getenv("LAUNCHER_DESKTOP")) desktop = o;  // tests
    else if (const char* d = SDL_GetUserFolder(SDL_FOLDER_DESKTOP)) desktop = d;
    if (desktop.empty()) desktop = pathToUtf8(paths_.dataDir);
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &now);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
    in.outZip = pathFromUtf8(desktop) / (std::string("STE-crash-report-") + stamp + ".zip");

    std::string err;
    if (!writeCrashReport(in, err)) {
        showModal("The crash report could not be saved", err, "");
        return;
    }
    showModal("Crash report saved",
              "The report is on your desktop: " + pathToUtf8(in.outZip.filename()) + "\n\n" + in.supportText +
                  "\n\nIt holds the crash dump, the game's logs and your game settings, no game files. Your user name and folders are removed from the "
                  "text files. The launcher sends nothing by itself.",
              "");
}
