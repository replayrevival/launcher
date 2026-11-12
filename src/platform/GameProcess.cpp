#include "platform/GameProcess.h"

#include <deque>
#include <fstream>

#include "core/identify.h"

namespace fs = std::filesystem;

GameProcess::~GameProcess() {
    if (process_) SDL_DestroyProcess(process_);
    closeStreams();
}

void GameProcess::closeStreams() {
    if (out_) { SDL_CloseIO(out_); out_ = nullptr; }
    if (err_) { SDL_CloseIO(err_); err_ = nullptr; }
}

bool GameProcess::start(const LaunchSpec& spec, std::string& err) {
    std::error_code ec;
    if (!fs::exists(spec.exe, ec)) {
        err = "The game program was not found: " + pathToUtf8(spec.exe);
        return false;
    }
    fs::create_directories(spec.logOut.parent_path(), ec);
    out_ = SDL_IOFromFile(pathToUtf8(spec.logOut).c_str(), "wb");
    err_ = SDL_IOFromFile(pathToUtf8(spec.logErr).c_str(), "wb");
    logErr_ = spec.logErr;
    logOut_ = spec.logOut;
    if (!out_ || !err_) {
        err = std::string("Could not create the game's log files: ") + SDL_GetError();
        closeStreams();
        return false;
    }

    SDL_Environment* env = SDL_CreateEnvironment(true);  // start from our own environment
    for (auto& [k, v] : spec.env) {
        if (v.empty()) SDL_UnsetEnvironmentVariable(env, k.c_str());
        else SDL_SetEnvironmentVariable(env, k.c_str(), v.c_str(), true);
    }

    const std::string exe = pathToUtf8(spec.exe);
    const std::string work = pathToUtf8(spec.workDir);
    std::vector<const char*> argv{exe.c_str()};
    for (auto& a : spec.args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, work.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, out_);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_POINTER, err_);
    // Not a "background" process: SDL reports the exit code of those as 0 whatever happened, which would hide crashes.
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, false);
    process_ = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    SDL_DestroyEnvironment(env);

    if (!process_) {
        err = std::string("The game could not be started: ") + SDL_GetError();
        closeStreams();
        return false;
    }
    startedNs_ = SDL_GetTicksNS();
    return true;
}

bool GameProcess::pollExited(int& exitCode) {
    if (!process_) return false;
    int code = 0;
    if (!SDL_WaitProcess(process_, false, &code)) return false;
    exitCode = code;
    SDL_DestroyProcess(process_);
    process_ = nullptr;
    closeStreams();
    return true;
}

double GameProcess::secondsRunning() const { return double(SDL_GetTicksNS() - startedNs_) / 1e9; }

void GameProcess::terminate() {
    if (process_) SDL_KillProcess(process_, false);
}

std::string GameProcess::errorTail(int maxLines) const { return tailOf(logErr_, maxLines); }
std::string GameProcess::outputTail(int maxLines) const { return tailOf(logOut_, maxLines); }

std::string GameProcess::tailOf(const std::filesystem::path& file, int maxLines) {
    std::ifstream f(file, std::ios::binary);
    std::deque<std::string> tail;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        tail.push_back(line.size() > 200 ? line.substr(0, 200) : line);
        if (int(tail.size()) > maxLines) tail.pop_front();
    }
    std::string out;
    for (auto& l : tail) out += l + "\n";
    return out;
}
