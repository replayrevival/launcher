#include "core/identify.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "core/sha256.h"
#include "core/xiso.h"

namespace fs = std::filesystem;

fs::path pathFromUtf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string pathToUtf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

std::string regionName(uint32_t flags) {
    if ((flags & 7) == 7) return "region free";
    std::string r;
    auto add = [&](const char* s) { r += (r.empty() ? "" : " + "); r += s; };
    if (flags & 1) add("NTSC (USA)");
    if (flags & 2) add("NTSC (Japan)");
    if (flags & 4) add("PAL (Europe / rest of world)");
    return r.empty() ? "unknown region" : r;
}

// Title name and region from the XBE certificate (head = the first bytes of default.xbe).
void readCertificate(const std::vector<uint8_t>& head, std::string& title, std::string& region) {
    if (head.size() < 0x140 || std::memcmp(head.data(), "XBEH", 4) != 0) return;
    const uint32_t base = le32(&head[0x104]), cert = le32(&head[0x118]);
    if (cert < base) return;
    const size_t o = cert - base;
    if (o + 0xA4 > head.size()) return;
    for (int i = 0; i < 40; ++i) {
        const uint16_t ch = uint16_t(head[o + 0x0C + i * 2] | head[o + 0x0D + i * 2] << 8);
        if (!ch) break;
        title += ch < 0x80 ? char(ch) : '?';
    }
    region = regionName(le32(&head[o + 0xA0]));
}

struct XbeBytes {
    std::vector<uint8_t> head;
    std::string sha256;
    bool ok = false;
};

template <class ReadFn>
XbeBytes hashXbe(uint64_t size, ReadFn read) {
    XbeBytes r;
    Sha256 sha;
    std::vector<uint8_t> chunk(1 << 20);
    for (uint64_t pos = 0; pos < size;) {
        const size_t n = size_t(std::min<uint64_t>(chunk.size(), size - pos));
        if (!read(pos, chunk.data(), n)) return r;
        if (pos == 0) r.head.assign(chunk.begin(), chunk.begin() + std::min<size_t>(n, 4096));
        sha.update(chunk.data(), n);
        pos += n;
    }
    r.sha256 = sha.finishHex();
    r.ok = true;
    return r;
}

IdResult finish(IdResult r, const XbeBytes& x, const IdExpect& expect) {
    r.sha256 = x.sha256;
    readCertificate(x.head, r.title, r.region);
    if (std::find(expect.sha256.begin(), expect.sha256.end(), x.sha256) != expect.sha256.end()) {
        r.status = IdStatus::Ok;
        r.message = "Found and verified. This matches a known good copy.";
    } else if (!expect.titleHint.empty() && lower(r.title).find(lower(expect.titleHint)) != std::string::npos) {
        r.status = IdStatus::WrongVersion;
        r.message = "This is the right game, but not a version the launcher knows" +
                    (r.region.empty() ? std::string() : " (" + r.region + ")") +
                    ". The launcher only supports original, unchanged copies of the version it lists. Yours may be another region, "
                    "a patched or repacked copy, or damaged. Try the original disc image or a fresh copy of the game files.";
    } else {
        r.status = IdStatus::WrongGame;
        r.message = r.title.empty() ? "This is a different game." : "This is a different game: " + r.title + ".";
    }
    return r;
}

}  // namespace

IdResult identifyGame(const fs::path& path, const IdExpect& expect) {
    IdResult r;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        r.status = IdStatus::NotFound;
        r.message = "The launcher cannot find that file or folder any more. It may have been moved or renamed, or the drive it is on is not connected. Pick it again, or let the launcher search your drives.";
        return r;
    }

    if (fs::is_directory(path, ec)) {
        r.kind = "folder";
        fs::path xbe;
        for (const auto& e : fs::directory_iterator(path, ec))
            if (lower(pathToUtf8(e.path().filename())) == "default.xbe") xbe = e.path();
        if (xbe.empty()) {
            r.status = IdStatus::NoXbe;
            r.message = "This folder does not contain a game. Choose the folder that holds default.xbe, or a disc image.";
            return r;
        }
        std::ifstream f(xbe, std::ios::binary);
        const uint64_t size = fs::file_size(xbe, ec);
        if (!f || ec) {
            r.status = IdStatus::CantRead;
            r.message = "Could not read the game files. Check that you have permission, and that no other program has them locked.";
            return r;
        }
        auto x = hashXbe(size, [&](uint64_t pos, void* buf, size_t n) {
            f.clear(); f.seekg(std::streamoff(pos)); f.read(static_cast<char*>(buf), std::streamsize(n));
            return size_t(f.gcount()) == n;
        });
        if (!x.ok) { r.status = IdStatus::CantRead; r.message = "Could not read default.xbe in that folder."; return r; }
        return finish(r, x, expect);
    }

    r.kind = "disc image";
    XisoImage img;
    if (img.open(path)) {
        uint64_t off = 0; uint32_t size = 0;
        if (!img.findRootFile("default.xbe", off, size)) {
            r.status = IdStatus::NoXbe;
            r.message = "This disc image has no default.xbe, so it is not an Xbox game disc.";
            return r;
        }
        auto x = hashXbe(size, [&](uint64_t pos, void* buf, size_t n) { return img.read(off + pos, buf, n); });
        if (!x.ok) { r.status = IdStatus::CantRead; r.message = "The disc image looks damaged or cut short: its game file could not be read completely."; return r; }
        return finish(r, x, expect);
    }

    // Not an Xbox image: say what it probably is.
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        r.status = IdStatus::CantRead;
        r.message = "Could not read that file. Check that you have permission, and that no other program has it open.";
        return r;
    }
    uint8_t m[0x20] = {};
    f.read(reinterpret_cast<char*>(m), sizeof m);
    const std::string ext = lower(pathToUtf8(path.extension()));
    const bool gc = le32(m + 0x1C) == 0x3D9F33C2u;  // GameCube magic, big-endian 0xC2339F3D
    if (std::memcmp(m, "WBFS", 4) == 0 || ext == ".wbfs" || gc || ext == ".gcm" || std::memcmp(m, "ZISO", 4) == 0 || ext == ".zso") {
        r.status = IdStatus::OtherPlatform;
        r.message = "This is a PlayStation 2, Wii or GameCube disc image. This launcher is for the Xbox version of the game.";
    } else {
        r.status = IdStatus::NotAGame;
        r.message = "This is not an Xbox disc image. Choose the game's .iso file, or the folder that holds default.xbe.";
    }
    return r;
}
