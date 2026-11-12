#pragma once
#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct LaunchSpec {
    std::filesystem::path exe;
    std::filesystem::path workDir;
    std::map<std::string, std::string> env;  // added to / overriding the launcher's own environment
    std::vector<std::string> args;
    std::filesystem::path logOut, logErr;
};

// Starts the game and watches it. stdout/stderr go to log files (like the .bat launchers did).
class GameProcess {
public:
    ~GameProcess();
    bool start(const LaunchSpec& spec, std::string& err);
    // Non-blocking. Returns true once the game has ended and fills exitCode.
    bool pollExited(int& exitCode);
    bool running() const { return process_ != nullptr; }
    double secondsRunning() const;
    // Last lines of the error log, for "why did it stop" messages.
    std::string errorTail(int maxLines = 8) const;
    std::string outputTail(int maxLines = 8) const;  // last lines of the normal log
    const std::filesystem::path& errLog() const { return logErr_; }
    const std::filesystem::path& outLog() const { return logOut_; }
    void terminate();  // asks the process to stop (used on launcher exit during tests)

private:
    void closeStreams();
    SDL_Process* process_ = nullptr;
    SDL_IOStream* out_ = nullptr;
    SDL_IOStream* err_ = nullptr;
    static std::string tailOf(const std::filesystem::path& file, int maxLines);
    std::filesystem::path logErr_, logOut_;
    Uint64 startedNs_ = 0;
};
