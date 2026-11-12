#include "platform/GameProcess.h"

#include <deque>
#include <fstream>

#include "core/identify.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

GameProcess::~GameProcess() {
#ifdef _WIN32
    if (winProcess_) CloseHandle(static_cast<HANDLE>(winProcess_));
#endif
    if (process_) SDL_DestroyProcess(process_);
    closeStreams();
}

void GameProcess::closeStreams() {
    if (out_) { SDL_CloseIO(out_); out_ = nullptr; }
    if (err_) { SDL_CloseIO(err_); err_ = nullptr; }
}


#ifdef _WIN32
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// One argument quoted the way CommandLineToArgvW reads it back.
std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    for (size_t i = 0; i < a.size(); ++i) {
        size_t slashes = 0;
        while (i < a.size() && a[i] == L'\\') { ++slashes; ++i; }
        if (i == a.size()) { out.append(slashes * 2, L'\\'); break; }
        if (a[i] == L'"') { out.append(slashes * 2 + 1, L'\\'); out += L'"'; }
        else { out.append(slashes, L'\\'); out += a[i]; }
    }
    return out + L"\"";
}

HANDLE openLog(const fs::path& p) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};  // inheritable: the game writes its stdout / stderr straight into the file
    return CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

}  // namespace

// Windows: our own CreateProcess instead of SDL's, so the game gets CREATE_NO_WINDOW (no console window flashing up for a console
// program) and still reports its real exit code (SDL only sets CREATE_NO_WINDOW for "background" processes, whose exit code it hides).
bool GameProcess::startWindows(const LaunchSpec& spec, std::string& err) {
    HANDLE out = openLog(spec.logOut), errH = openLog(spec.logErr);
    if (out == INVALID_HANDLE_VALUE || errH == INVALID_HANDLE_VALUE) {
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (errH != INVALID_HANDLE_VALUE) CloseHandle(errH);
        err = "Could not create the game's log files in " + pathToUtf8(spec.logOut.parent_path());
        return false;
    }
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    // Environment block: ours, plus / minus what the profile and the settings ask for.
    std::map<std::wstring, std::wstring> env;
    if (LPWCH block = GetEnvironmentStringsW()) {
        for (const wchar_t* e = block; *e; e += wcslen(e) + 1) {
            const wchar_t* eq = wcschr(e + 1, L'=');  // names may start with '=' (drive cwd entries)
            if (eq) env[std::wstring(e, eq)] = eq + 1;
        }
        FreeEnvironmentStringsW(block);
    }
    for (auto& [k, v] : spec.env) {
        if (v.empty()) env.erase(widen(k));
        else env[widen(k)] = widen(v);
    }
    std::wstring envBlock;
    for (auto& [k, v] : env) { envBlock += k + L"=" + v; envBlock += L'\0'; }
    envBlock += L'\0';

    std::wstring cmd = quoteArg(spec.exe.wstring());
    for (auto& a : spec.args) cmd += L" " + quoteArg(widen(a));

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    si.hStdOutput = out;
    si.hStdError = errH;
    PROCESS_INFORMATION pi{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | (spec.showConsole ? 0 : CREATE_NO_WINDOW);
    const std::wstring work = spec.workDir.wstring();
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, flags, envBlock.data(), work.empty() ? nullptr : work.c_str(), &si, &pi);
    const DWORD lastErr = GetLastError();
    CloseHandle(out);
    CloseHandle(errH);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        err = "The game could not be started (Windows error " + std::to_string(lastErr) + ").";
        return false;
    }
    CloseHandle(pi.hThread);
    winProcess_ = pi.hProcess;
    startedNs_ = SDL_GetTicksNS();
    return true;
}
#endif

bool GameProcess::start(const LaunchSpec& spec, std::string& err) {
    std::error_code ec;
    if (!fs::exists(spec.exe, ec)) {
        err = "The game program was not found: " + pathToUtf8(spec.exe);
        return false;
    }
    fs::create_directories(spec.logOut.parent_path(), ec);
    logErr_ = spec.logErr;
    logOut_ = spec.logOut;
#ifdef _WIN32
    return startWindows(spec, err);
#endif
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
#ifdef _WIN32
    if (winProcess_) {
        if (WaitForSingleObject(static_cast<HANDLE>(winProcess_), 0) != WAIT_OBJECT_0) return false;
        DWORD code = 0;
        GetExitCodeProcess(static_cast<HANDLE>(winProcess_), &code);
        exitCode = int(code);
        CloseHandle(static_cast<HANDLE>(winProcess_));
        winProcess_ = nullptr;
        return true;
    }
#endif
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
#ifdef _WIN32
    if (winProcess_) TerminateProcess(static_cast<HANDLE>(winProcess_), 1);
#endif
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
