#include "platform/UpdateFetch.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    explicit operator bool() const { return h != nullptr; }
};

}  // namespace

bool httpGet(const std::string& url, const std::string& userAgent, int timeoutMs, std::string& body) {
    body.clear();
    const std::wstring wurl = widen(url);
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;  uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && uc.nScheme != INTERNET_SCHEME_HTTP) return false;

    Handle session{WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) return false;
    WinHttpSetTimeouts(session.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    Handle conn{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!conn) return false;
    Handle req{WinHttpOpenRequest(conn.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!req) return false;
    const wchar_t* hdr = L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpSendRequest(req.h, hdr, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(req.h, nullptr)) return false;
    DWORD status = 0, sz = sizeof status;
    if (!WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX) ||
        status != 200)
        return false;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(req.h, buf.data(), avail, &got) || got == 0) break;
        body.append(buf.data(), got);
        if (body.size() > 512 * 1024) return false;  // a release list is a few KB; anything huge is not what we asked for
    }
    return !body.empty();
}

#else

bool httpGet(const std::string&, const std::string&, int, std::string& body) {
    body.clear();
    return false;
}

#endif
