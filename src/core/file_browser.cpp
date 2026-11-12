#include "core/file_browser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "core/identify.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxEntries = 1500;

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isHiddenOrSystem(const fs::path& p) {
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(p.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) return true;
#endif
    const std::string n = pathToUtf8(p.filename());
    return !n.empty() && (n[0] == '$' || n == "System Volume Information");
}

bool isFolder(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

}  // namespace

std::string FileBrowser::sizeText(uint64_t b) {
    char buf[32];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", double(b) / double(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", double(b) / double(1ull << 20));
    else if (b >= 1024) std::snprintf(buf, sizeof buf, "%.0f KB", double(b) / 1024.0);
    else std::snprintf(buf, sizeof buf, "%llu bytes", static_cast<unsigned long long>(b));
    return buf;
}

void FileBrowser::open(BrowseKind kind, const std::string& title, std::vector<std::string> exts, const fs::path& start,
                       std::vector<fs::path> recents) {
    kind_ = kind;
    title_ = title;
    exts_.clear();
    for (auto& e : exts) exts_.push_back(lowerAscii(e));
    recents_.clear();
    for (auto& r : recents)
        if (isFolder(r)) recents_.push_back(r);
    // quick places
    places_.clear();
    const char* home = std::getenv("USERPROFILE");
    const char* oneDrive = std::getenv("OneDrive");
    for (const char* name : {"Desktop", "Documents", "Downloads"}) {
        fs::path p;
        if (oneDrive && isFolder(fs::path(oneDrive) / name)) p = fs::path(oneDrive) / name;
        else if (home && isFolder(fs::path(home) / name)) p = fs::path(home) / name;
        if (!p.empty()) places_.push_back({name, p});
    }
    fs::path first = start;
    if (!isFolder(first) && !recents_.empty()) first = recents_.front();
    if (!isFolder(first)) {
        for (auto& p : places_)
            if (p.label == "Documents") first = p.path;
    }
    if (isFolder(first)) goTo(first);
    else goDriveList();
}

bool FileBrowser::matches(const fs::path& file) const {
    if (exts_.empty()) return true;
    const std::string e = lowerAscii(pathToUtf8(file.extension()));
    return std::find(exts_.begin(), exts_.end(), e) != exts_.end();
}

void FileBrowser::goDriveList() {
    dir_.clear();
    refresh();
}

void FileBrowser::goTo(const fs::path& p) {
    dir_ = p;
    refresh();
}

std::string FileBrowser::pathText() const { return dir_.empty() ? std::string("This PC") : pathToUtf8(dir_); }

void FileBrowser::refresh() {
    entries_.clear();
    truncated_ = false;
    if (dir_.empty()) {
#ifdef _WIN32
        const DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (!(mask & (1u << i))) continue;
            const std::wstring root = std::wstring(1, wchar_t(L'A' + i)) + L":\\";
            const UINT t = GetDriveTypeW(root.c_str());
            const char* kind = t == DRIVE_REMOVABLE ? "Removable drive" : t == DRIVE_REMOTE ? "Network drive" : t == DRIVE_CDROM ? "Disc drive"
                               : t == DRIVE_FIXED ? "Local disk" : "Drive";
            BrowseEntry e;
            e.dir = true;
            e.path = fs::path(root);
            e.name = pathToUtf8(e.path) + " (" + kind + ")";
            entries_.push_back(std::move(e));
        }
#else
        BrowseEntry e;
        e.dir = true;
        e.path = fs::path("/");
        e.name = "/ (Computer)";
        entries_.push_back(std::move(e));
#endif
        return;
    }
    std::vector<BrowseEntry> dirs, files;
    std::error_code ec;
    fs::directory_iterator it(dir_, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const fs::path p = it->path();
        const bool isDir = it->is_directory(e2);
        if (e2) continue;
        if (isHiddenOrSystem(p)) continue;
        if (!isDir && (kind_ == BrowseKind::Folder || !matches(p))) continue;
        BrowseEntry e;
        e.dir = isDir;
        e.path = p;
        e.name = pathToUtf8(p.filename());
        if (!isDir) {
            e.size = it->file_size(e2);
            if (e2) e.size = 0;
        }
        (isDir ? dirs : files).push_back(std::move(e));
        if (dirs.size() + files.size() > kMaxEntries * 2) break;
    }
    auto byName = [](const BrowseEntry& a, const BrowseEntry& b) { return lowerAscii(a.name) < lowerAscii(b.name); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    for (auto& d : dirs) entries_.push_back(std::move(d));
    for (auto& f : files) entries_.push_back(std::move(f));
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
        truncated_ = true;
    }
}

bool FileBrowser::enter(size_t idx, fs::path& picked) {
    if (idx >= entries_.size()) return false;
    const BrowseEntry e = entries_[idx];  // copy: goTo() replaces the list
    if (e.dir) {
        goTo(e.path);
        return false;
    }
    picked = e.path;
    return true;
}

bool FileBrowser::up() {
    if (dir_.empty()) return false;
    const fs::path parent = dir_.parent_path();
    if (parent.empty() || parent == dir_) {
        goDriveList();
    } else {
        goTo(parent);
    }
    return true;
}
