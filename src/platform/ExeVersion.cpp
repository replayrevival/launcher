#include "platform/ExeVersion.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

std::string readExeVersion(const std::filesystem::path& exe) {
    const std::wstring w = exe.wstring();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(w.c_str(), &handle);
    if (!size) return "";
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(w.c_str(), 0, size, data.data())) return "";

    struct LangCp { WORD lang, cp; } *tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &len) || len < sizeof(LangCp))
        return "";
    wchar_t sub[64];
    swprintf(sub, 64, L"\\StringFileInfo\\%04x%04x\\ProductVersion", tr->lang, tr->cp);
    wchar_t* val = nullptr;
    if (!VerQueryValueW(data.data(), sub, reinterpret_cast<void**>(&val), &len) || !val) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, val, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, val, -1, out.data(), n, nullptr, nullptr);
    return out;
}
#else
std::string readExeVersion(const std::filesystem::path&) { return ""; }
#endif
