#include "platform/DriveSearch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr int kMaxDepth = 5;
constexpr uint64_t kMinImageBytes = 200ull * 1024 * 1024;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool skipDir(const std::string& name) {
    static const char* skip[] = {"windows", "program files", "program files (x86)", "programdata", "$recycle.bin",
                                 "system volume information", "appdata", "node_modules", ".git", "proc", "sys", "dev"};
    const std::string n = lower(name);
    for (auto s : skip)
        if (n == s) return true;
    return false;
}

std::vector<fs::path> searchRoots() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        const wchar_t root[] = {wchar_t(L'A' + i), L':', L'\\', 0};
        const UINT t = GetDriveTypeW(root);
        if (t == DRIVE_FIXED || t == DRIVE_REMOVABLE) roots.emplace_back(root);
    }
#else
    for (const char* r : {"/media", "/mnt", "/run/media"}) roots.emplace_back(r);
    if (const char* home = std::getenv("HOME")) roots.emplace_back(home);
#endif
    return roots;
}

}  // namespace

void DriveSearch::start(const IdExpect& expect) {
    cancel();
    {
        std::lock_guard<std::mutex> l(mu_);
        found_.clear();
        current_.clear();
    }
    cancel_ = false;
    running_ = true;
    thread_ = std::thread([this, expect] { run(expect); });
}

void DriveSearch::cancel() {
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

std::string DriveSearch::poll(std::vector<FoundGame>& out) {
    std::lock_guard<std::mutex> l(mu_);
    for (auto& f : found_) out.push_back(std::move(f));
    found_.clear();
    return current_;
}

void DriveSearch::run(IdExpect expect) {
    auto consider = [&](const fs::path& p) {
        IdResult r = identifyGame(p, expect);
        if (r.status == IdStatus::Ok || r.status == IdStatus::WrongVersion || r.status == IdStatus::WrongGame) {
            std::lock_guard<std::mutex> l(mu_);
            found_.push_back({p, std::move(r)});
        }
    };

    std::vector<std::pair<fs::path, int>> stack;
    for (auto& r : searchRoots()) stack.push_back({r, 0});
    std::reverse(stack.begin(), stack.end());

    while (!stack.empty() && !cancel_) {
        auto [dir, depth] = stack.back();
        stack.pop_back();
        {
            std::lock_guard<std::mutex> l(mu_);
            current_ = pathToUtf8(dir);
        }
        std::error_code ec;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        std::vector<fs::path> subdirs;
        bool hasXbe = false;
        for (; !ec && it != end && !cancel_; it.increment(ec)) {
            std::error_code e2;
            const fs::path p = it->path();
            const std::string name = pathToUtf8(p.filename());
            if (it->is_directory(e2)) {
                if (depth < kMaxDepth && !skipDir(name)) subdirs.push_back(p);
            } else if (it->is_regular_file(e2)) {
                const std::string n = lower(name);
                if (n == "default.xbe") hasXbe = true;
                const std::string ext = lower(pathToUtf8(p.extension()));
                if ((ext == ".iso" || ext == ".xiso") && it->file_size(e2) >= kMinImageBytes) consider(p);
            }
        }
        if (hasXbe) consider(dir);
        for (auto it2 = subdirs.rbegin(); it2 != subdirs.rend(); ++it2) stack.push_back({*it2, depth + 1});
    }
    running_ = false;
}
