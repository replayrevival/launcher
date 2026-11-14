#pragma once
// "A new version is available": reading GitHub's release list and comparing versions. No network code here (platform/UpdateFetch does
// the request); the launcher only ever tells the player and offers a link, it never downloads anything.
#include <string>

struct UpdateInfo {
    std::string tag;  // "v0.1.4"
    std::string url;  // the release page (https://github.com/...)
};

// Picks the newest published (not draft) release from the JSON of GET /repos/<owner>/<repo>/releases, or a single release object.
// Returns false for anything unexpected (rate limit message, not found, garbage).
bool parseLatestRelease(const std::string& json, UpdateInfo& out);

// Compares dotted version numbers ("0.1.3", "v0.1.4-alpha", "1.0"): the leading numbers decide, then a hotfix letter right after them (0.1.3 < 0.1.3a < 0.1.4); other text is ignored.
// Returns <0, 0, >0.
int compareVersions(const std::string& a, const std::string& b);

// True when `tag` names a newer version than `current`.
bool isNewerVersion(const std::string& tag, const std::string& current);

// Only links to github.com are ever opened from an update note.
bool isSafeReleaseUrl(const std::string& url);
