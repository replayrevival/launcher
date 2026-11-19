#include "core/xiso.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t kSector = 2048;
// raw (extract-xiso style) and the game-partition starts of redump-style images with a video partition in front (the same list the game uses):
// 0x18300000 (XGD1), 0xFD90000 (XGD2), 0x2090000 (XGD3, as in extract-xiso). 0x2080000 was this list's old XGD3 value and stays as a last guess.
const uint64_t kBases[] = {0, 0x18300000ull, 0xFD90000ull, 0x2090000ull, 0x2080000ull};

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}  // namespace

bool XisoImage::read(uint64_t offset, void* buf, size_t n) {
    f_.clear();
    f_.seekg(std::streamoff(offset));
    f_.read(static_cast<char*>(buf), std::streamsize(n));
    return size_t(f_.gcount()) == n;
}

bool XisoImage::open(const std::filesystem::path& path) {
    base_ = kNone;
    f_.close();
    f_.open(path, std::ios::binary);
    if (!f_) return false;
    for (uint64_t base : kBases) {
        uint8_t vd[0x20];
        if (!read(base + 0x10000, vd, sizeof vd)) continue;
        if (std::memcmp(vd, "MICROSOFT*XBOX*MEDIA", 20) != 0) continue;
        base_ = base;
        rootSector_ = le32(vd + 0x14);
        rootSize_ = le32(vd + 0x18);
        if (rootSize_ == 0 || rootSize_ > 16 * 1024 * 1024) { base_ = kNone; continue; }
        return true;
    }
    return false;
}

bool XisoImage::visit(const std::vector<uint8_t>& dir, uint32_t pos, const std::string& want, uint64_t& off,
                      uint32_t& size, int depth) {
    if (depth > 64 || pos + 14 > dir.size()) return false;
    const uint8_t* e = &dir[pos];
    if (le16(e) == 0xFFFF && le16(e + 2) == 0xFFFF) return false;  // padding
    const uint16_t left = le16(e), right = le16(e + 2);
    const uint32_t sector = le32(e + 4), len = le32(e + 8);
    const uint8_t nameLen = e[13];
    if (pos + 14 + nameLen > dir.size()) return false;
    const std::string name(reinterpret_cast<const char*>(e + 14), nameLen);
    if (iequals(name, want)) {
        off = base_ + uint64_t(sector) * kSector;
        size = len;
        return true;
    }
    if (left && visit(dir, uint32_t(left) * 4, want, off, size, depth + 1)) return true;
    if (right && visit(dir, uint32_t(right) * 4, want, off, size, depth + 1)) return true;
    return false;
}

bool XisoImage::findRootFile(const std::string& name, uint64_t& offset, uint32_t& size) {
    if (!isXbox()) return false;
    std::vector<uint8_t> dir(rootSize_);
    if (!read(base_ + uint64_t(rootSector_) * kSector, dir.data(), dir.size())) return false;
    return visit(dir, 0, name, offset, size, 0);
}
