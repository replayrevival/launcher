#pragma once
#include <string>

// One HTTPS (or http:// for tests) GET with short timeouts, for the update check. Windows uses WinHTTP (part of the system, no extra
// library); other platforms return false until a port adds a backend. Never throws, never prompts: false on any problem (offline,
// proxy, timeout, status other than 200), so the caller can stay quiet.
bool httpGet(const std::string& url, const std::string& userAgent, int timeoutMs, std::string& body);
