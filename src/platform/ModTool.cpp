#include "platform/ModTool.h"

#include <cstdlib>
#include <string>

#include "core/identify.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string envUtf8(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

fs::path findPython(const fs::path& exeDir) {
    if (const std::string e = envUtf8("LAUNCHER_PYTHON"); !e.empty()) return pathFromUtf8(e);
    std::error_code ec;
    const fs::path bundled = exeDir / "tools" / "python" / "python.exe";
    if (fs::is_regular_file(bundled, ec)) return bundled;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    DWORD n = SearchPathW(nullptr, L"python.exe", nullptr, (DWORD)(sizeof(buf) / sizeof(buf[0])), buf, nullptr);
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) {
        const fs::path p(buf);
        // The Microsoft Store stub (WindowsApps) only opens the Store; ignore it.
        if (p.string().find("WindowsApps") == std::string::npos) return p;
    }
#endif
    return {};
}

fs::path findScript(const fs::path& exeDir) {
    if (const std::string e = envUtf8("LAUNCHER_MOD_TOOL"); !e.empty()) return pathFromUtf8(e);
    return exeDir / "tools" / "mods" / "iso_to_mod.py";
}

}  // namespace

ExternalResult runModTool(const fs::path& exeDir, const fs::path& modDir, const fs::path& gameDir, const std::atomic<bool>* cancel) {
    ExternalResult r;
    std::error_code ec;
    const fs::path script = findScript(exeDir);
    if (!fs::is_regular_file(script, ec)) {
        r.startError = "This mod is built on your PC, and the helper that builds it is not in this download yet.";
        return r;
    }
    const fs::path py = findPython(exeDir);
    if (py.empty()) {
        r.startError = "This mod is built on your PC and needs Python 3.9 or newer. Install Python from python.org, then try again.";
        return r;
    }
#ifdef _WIN32
    auto q = [](const fs::path& p) { return L"\"" + p.wstring() + L"\""; };
    std::wstring cmd = q(py) + L" -I " + q(script) + L" prepare " + q(modDir) + L" " + q(gameDir);
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE pipeIn = nullptr, pipeOut = nullptr;
    if (!CreatePipe(&pipeIn, &pipeOut, &sa, 0)) { r.startError = "Could not start the helper (pipe)."; return r; }
    SetHandleInformation(pipeIn, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = pipeOut;
    si.hStdError = pipeOut;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pipeIn);
        CloseHandle(pipeOut);
        r.startError = "Could not start Python (error " + std::to_string(GetLastError()) + ").";
        return r;
    }
    CloseHandle(pipeOut);
    r.started = true;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(pipeIn, buf, sizeof buf, &got, nullptr) && got) {
        r.output.append(buf, got);
        if (cancel && cancel->load()) { TerminateProcess(pi.hProcess, 3); break; }
        if (r.output.size() > (4u << 20)) break;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(pipeIn);
#else
    r.startError = "Building recipe mods is only supported on Windows.";
#endif
    return r;
}
