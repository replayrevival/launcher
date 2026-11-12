#include "core/update_check.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

std::vector<long> numbersOf(const std::string& v) {
    std::vector<long> out;
    size_t i = 0;
    while (i < v.size() && !std::isdigit(static_cast<unsigned char>(v[i]))) ++i;  // skip "v"
    while (i < v.size()) {
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) break;
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = n * 10 + (v[i++] - '0');
        out.push_back(n);
        if (i < v.size() && v[i] == '.') ++i;
        else break;
    }
    return out;
}

bool releaseFrom(const nlohmann::json& r, UpdateInfo& out) {
    if (!r.is_object() || r.value("draft", false)) return false;
    const std::string tag = r.value("tag_name", std::string());
    if (tag.empty() || numbersOf(tag).empty()) return false;
    out.tag = tag;
    out.url = r.value("html_url", std::string());
    return true;
}

}  // namespace

bool parseLatestRelease(const std::string& json, UpdateInfo& out) {
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) return false;
    if (j.is_array()) {
        UpdateInfo best;
        bool any = false;
        for (auto& r : j) {
            UpdateInfo u;
            if (!releaseFrom(r, u)) continue;
            if (!any || compareVersions(u.tag, best.tag) > 0) { best = u; any = true; }
        }
        if (any) out = best;
        return any;
    }
    return releaseFrom(j, out);
}

int compareVersions(const std::string& a, const std::string& b) {
    const auto x = numbersOf(a), y = numbersOf(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        const long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

bool isNewerVersion(const std::string& tag, const std::string& current) { return compareVersions(tag, current) > 0; }

bool isSafeReleaseUrl(const std::string& url) { return url.rfind("https://github.com/", 0) == 0; }
