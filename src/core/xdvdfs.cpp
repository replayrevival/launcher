#include "core/xdvdfs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <thread>

#include "core/identify.h"  // pathFromUtf8 / pathToUtf8

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSector = 2048;
// Where the game partition starts inside the image: trimmed/raw XISO, XGD1, XGD2, XGD3.
const uint64_t kPartitionStarts[] = {0, 0x18300000ull, 0xFD90000ull, 0x2080000ull};
constexpr size_t kMaxEntries = 200000;
constexpr int kMaxDepth = 40;
constexpr uint32_t kMaxDirBytes = 32u * 1024 * 1024;

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool reservedWindowsName(const std::string& name) {
    std::string stem = lowerAscii(name.substr(0, name.find('.')));
    while (!stem.empty() && stem.back() == ' ') stem.pop_back();
    static const char* fixed[] = {"con", "prn", "aux", "nul", "conin$", "conout$"};
    for (auto* f : fixed) if (stem == f) return true;
    if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '1' && stem[3] <= '9') return true;
    return false;
}

struct Image {
    std::ifstream f;
    uint64_t size = 0;
    uint64_t base = 0;
    uint32_t rootSector = 0, rootSize = 0;

    bool open(const fs::path& p, std::string& err) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) { err = "That is not a file."; return false; }
        size = fs::file_size(p, ec);
        f.open(p, std::ios::binary);   // read-only
        if (!f) { err = "Could not read the disc image. Check that you have permission, and that no other program has it open."; return false; }
        for (uint64_t b : kPartitionStarts) {
            uint8_t vd[0x20];
            if (!read(b + 0x10000, vd, sizeof vd)) continue;
            if (std::memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20) != 0) continue;
            base = b;
            rootSector = le32(vd + 0x14);
            rootSize = le32(vd + 0x18);
            if (rootSize == 0 || rootSize > kMaxDirBytes) continue;
            if (base + uint64_t(rootSector) * kSector + rootSize > size) continue;
            return true;
        }
        err = "This is not an Xbox disc image, or it is damaged.";
        return false;
    }
    bool read(uint64_t off, void* buf, size_t n) {
        if (off + n > size) return false;
        f.clear();
        f.seekg(std::streamoff(off));
        f.read(static_cast<char*>(buf), std::streamsize(n));
        return size_t(f.gcount()) == n;
    }
};

struct Walker {
    Image& img;
    std::vector<XdvdfsEntry>& out;
    uint64_t totalBytes = 0;
    std::string err;

    bool fail(const std::string& m) { if (err.empty()) err = m; return false; }

    bool walkDir(uint32_t sector, uint32_t size, const std::string& prefix, int depth) {
        if (depth > kMaxDepth) return fail("The disc image has folders nested too deeply.");
        if (size == 0) return true;
        if (size > kMaxDirBytes) return fail("The disc image has an unusually large folder table, so it was not unpacked.");
        const uint64_t off = img.base + uint64_t(sector) * kSector;
        if (off + size > img.size) return fail("The disc image looks cut short: a folder table lies past its end.");
        std::vector<uint8_t> dir(size);
        if (!img.read(off, dir.data(), dir.size())) return fail("The disc image could not be read completely.");
        if (size >= 4 && le16(&dir[0]) == 0xFFFF && le16(&dir[2]) == 0xFFFF) return true;  // empty

        struct Sub { uint32_t sector, size; std::string path; };
        std::vector<Sub> subs;
        std::set<std::string> seen;
        std::set<uint32_t> visited;
        std::vector<uint32_t> stack{0};
        while (!stack.empty()) {
            const uint32_t pos = stack.back();
            stack.pop_back();
            if (pos + 14 > dir.size()) return fail("The disc image has a damaged folder table.");
            if (!visited.insert(pos).second) return fail("The disc image has a damaged folder table (a loop).");
            const uint8_t* e = &dir[pos];
            const uint16_t left = le16(e), right = le16(e + 2);
            const uint32_t sec = le32(e + 4), len = le32(e + 8);
            const uint8_t attr = e[12], nameLen = e[13];
            if (pos + 14 + nameLen > dir.size() || nameLen == 0) return fail("The disc image has a damaged folder table.");
            std::string raw(reinterpret_cast<const char*>(e + 14), nameLen), name, why;
            if (!xdvdfsNameOk(raw, name, why)) return fail("The disc image contains a file name that is not safe to unpack (" + why + ").");
            if (!seen.insert(lowerAscii(name)).second) return fail("The disc image contains two files with the same name in one folder (" + name + "), which Windows cannot store.");
            if (out.size() >= kMaxEntries) return fail("The disc image has far more files than a game disc should, so it was not unpacked.");
            const std::string rel = prefix.empty() ? name : prefix + "/" + name;
            if (attr & 0x10) {
                out.push_back({rel, true, 0, 0});
                subs.push_back({sec, len, rel});
            } else {
                const uint64_t foff = img.base + uint64_t(sec) * kSector;
                if (len && foff + len > img.size) return fail("The disc image looks cut short: " + rel + " lies past its end.");
                out.push_back({rel, false, foff, len});
                totalBytes += len;
            }
            if (right) stack.push_back(uint32_t(right) * 4);
            if (left) stack.push_back(uint32_t(left) * 4);
        }
        for (auto& s : subs)
            if (!walkDir(s.sector, s.size, s.path, depth + 1)) return false;
        return true;
    }
};

bool isInside(const fs::path& child, const fs::path& parent) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(child, ec), p = fs::weakly_canonical(parent, ec);
    auto ci = c.begin(), pi = p.begin();
    for (; pi != p.end(); ++pi, ++ci)
        if (ci == c.end() || lowerAscii(pathToUtf8(*ci)) != lowerAscii(pathToUtf8(*pi))) return false;
    return true;
}

}  // namespace

bool xdvdfsNameOk(const std::string& raw, std::string& utf8, std::string& why) {
    utf8.clear();
    if (raw.empty()) { why = "empty name"; return false; }
    if (raw == "." || raw == "..") { why = "dot name"; return false; }
    for (unsigned char c : raw) {
        if (c < 0x20 || c == 0x7F) { why = "control character"; return false; }
        if (std::strchr("<>:\"/\\|?*", c)) { why = std::string("character '") + char(c) + "'"; return false; }
    }
    if (raw.back() == ' ' || raw.back() == '.') { why = "trailing space or dot"; return false; }
    if (raw.size() > 255) { why = "too long"; return false; }
    for (unsigned char c : raw) {   // names on the disc are single-byte (Latin-1): store as UTF-8
        if (c < 0x80) utf8 += char(c);
        else { utf8 += char(0xC0 | (c >> 6)); utf8 += char(0x80 | (c & 0x3F)); }
    }
    if (reservedWindowsName(utf8)) { why = "reserved Windows name"; return false; }
    return true;
}

bool xdvdfsList(const fs::path& image, std::vector<XdvdfsEntry>& out, uint64_t& totalBytes, std::string& err) {
    out.clear();
    totalBytes = 0;
    Image img;
    if (!img.open(image, err)) return false;
    Walker w{img, out, 0, {}};
    if (!w.walkDir(img.rootSector, img.rootSize, "", 0)) { err = w.err; return false; }
    totalBytes = w.totalBytes;
    return true;
}

std::string unpackPreflight(const fs::path& image, const fs::path& finalDirIn, uint64_t bytes) {
    std::error_code ec;
    const fs::path finalAbs = fs::absolute(finalDirIn, ec);
    fs::path partial = finalAbs;
    partial += ".partial";
    if (fs::exists(finalAbs, ec)) return "The folder " + pathToUtf8(finalAbs) + " already exists. Choose another place to unpack to.";
    const fs::path imgDir = fs::absolute(image, ec).parent_path();
    if (isInside(finalAbs, imgDir) || isInside(partial, imgDir))
        return "Please unpack to a different folder: the unpacked game must not be placed in the folder that holds your disc image.";
    fs::path probe = finalAbs.parent_path();
    while (!probe.empty() && !fs::exists(probe, ec) && probe != probe.parent_path()) probe = probe.parent_path();
    uint64_t avail = ~0ull;
    if (const char* fake = std::getenv("LAUNCHER_FAKE_FREE_MB")) avail = uint64_t(std::atoll(fake)) * 1024 * 1024;
    else { auto sp = fs::space(probe, ec); if (!ec) avail = sp.available; }
    if (avail < bytes + kUnpackSlackBytes) {
        char b[256];
        std::snprintf(b, sizeof b, "There is not enough free space on that drive: the game needs about %.1f GB and %.1f GB is free. Free some space, or choose another drive for the unpacked game.",
                      double(bytes) / 1073741824.0, double(avail) / 1073741824.0);
        return b;
    }
    return std::string();
}

UnpackResult unpackImage(const fs::path& image, const fs::path& finalDir, UnpackProgress& pr) {
    UnpackResult res;
    std::error_code ec;
    std::vector<XdvdfsEntry> entries;
    uint64_t total = 0;
    if (!xdvdfsList(image, entries, total, res.error)) return res;

    fs::path finalAbs = fs::absolute(finalDir, ec);
    fs::path partial = finalAbs;
    partial += ".partial";
    res.error = unpackPreflight(image, finalAbs, total);
    if (!res.error.empty()) return res;

    uint32_t nfiles = 0;
    for (auto& e : entries) if (!e.dir) ++nfiles;
    pr.bytesTotal = total;
    pr.filesTotal = nfiles;
    pr.bytesDone = 0;
    pr.filesDone = 0;

    // Start clean: a left-over ".partial" from an earlier cancelled run is ours and is replaced.
    if (fs::exists(partial, ec) && partial.extension() == ".partial") fs::remove_all(partial, ec);
    fs::create_directories(partial, ec);
    if (ec) { res.error = "Could not create the folder " + pathToUtf8(partial) + ": " + ec.message(); return res; }

    auto abort = [&](const std::string& msg, bool cancelled) {
        res.ok = false; res.cancelled = cancelled; res.error = cancelled ? std::string() : msg;
        std::error_code e2;
        if (partial.extension() == ".partial") fs::remove_all(partial, e2);   // only our own temporary folder
        return res;
    };

    std::ifstream in(image, std::ios::binary);   // read-only
    if (!in) return abort("Could not read the disc image. Check that no other program has it open.", false);
    const std::string rootKey = lowerAscii(pathToUtf8(partial.lexically_normal()));
    std::vector<char> buf(1 << 20);

    for (const auto& e : entries) {
        if (pr.cancel) return abort("", true);
        const fs::path dest = (partial / pathFromUtf8(e.path)).lexically_normal();
        const std::string key = lowerAscii(pathToUtf8(dest));
        if (key.size() <= rootKey.size() + 1 || key.compare(0, rootKey.size(), rootKey) != 0 || (key[rootKey.size()] != '\\' && key[rootKey.size()] != '/'))
            return abort("Refused to write outside the target folder (" + e.path + ").", false);
        if (e.dir) {
            fs::create_directories(dest, ec);
            if (ec) return abort("Could not create the folder " + pathToUtf8(dest) + ": " + ec.message(), false);
            continue;
        }
        fs::create_directories(dest.parent_path(), ec);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) return abort("Could not write " + pathToUtf8(dest) + ". Check that the drive is not full or read-only.", false);
        uint64_t left = e.size, pos = e.offset;
        while (left) {
            if (pr.cancel) { out.close(); return abort("", true); }
            const size_t n = size_t(std::min<uint64_t>(buf.size(), left));
            in.clear();
            in.seekg(std::streamoff(pos));
            in.read(buf.data(), std::streamsize(n));
            if (size_t(in.gcount()) != n) return abort("The disc image could not be read completely (" + e.path + "). It may be damaged.", false);
            out.write(buf.data(), std::streamsize(n));
            if (!out) return abort("Writing " + e.path + " failed. The drive may be full.", false);
            pos += n; left -= n;
            pr.bytesDone += n;
            if (const uint32_t t = pr.throttleMs) std::this_thread::sleep_for(std::chrono::milliseconds(t));
        }
        out.close();
        if (!out) return abort("Writing " + e.path + " failed. The drive may be full.", false);
        ++pr.filesDone;
    }
    if (pr.cancel) return abort("", true);
    in.close();
    fs::rename(partial, finalAbs, ec);
    if (ec) return abort("The unpacked files could not be moved into place: " + ec.message(), false);
    res.ok = true;
    return res;
}
