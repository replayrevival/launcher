#include "platform/png_decode.h"

#include <miniz.h>

#include <cstdlib>
#include <cstring>

namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
}  // namespace

bool decodePng(const std::vector<uint8_t>& f, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (f.size() < 8 || std::memcmp(f.data(), sig, 8) != 0) { err = "not a PNG file"; return false; }
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = -1, interlace = 0;
    std::vector<uint8_t> palette, trns, idat;
    for (size_t pos = 8; pos + 12 <= f.size();) {
        const uint32_t len = be32(&f[pos]);
        if (len > f.size() || pos + 12 + len > f.size()) { err = "truncated PNG"; return false; }
        const char* type = reinterpret_cast<const char*>(&f[pos + 4]);
        const uint8_t* d = &f[pos + 8];
        if (!std::memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(d); h = be32(d + 4); depth = d[8]; ctype = d[9]; interlace = d[12];
        } else if (!std::memcmp(type, "PLTE", 4)) palette.assign(d, d + len);
        else if (!std::memcmp(type, "tRNS", 4)) trns.assign(d, d + len);
        else if (!std::memcmp(type, "IDAT", 4)) idat.insert(idat.end(), d, d + len);
        else if (!std::memcmp(type, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (w == 0 || h == 0 || w > 4096 || h > 4096) { err = "unsupported PNG size"; return false; }
    if (interlace != 0) { err = "interlaced PNG not supported"; return false; }
    int channels = 0;
    switch (ctype) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: err = "unsupported PNG colour type"; return false;
    }
    if (ctype == 3 ? !(depth == 1 || depth == 2 || depth == 4 || depth == 8) : depth != 8) { err = "unsupported PNG bit depth"; return false; }
    if (ctype == 3 && palette.size() < 3) { err = "PNG palette missing"; return false; }

    const size_t bitsPerPixel = size_t(channels) * depth;
    const size_t stride = (size_t(w) * bitsPerPixel + 7) / 8;
    const size_t bpp = (bitsPerPixel + 7) / 8;
    size_t rawLen = 0;
    void* raw = tinfl_decompress_mem_to_heap(idat.data(), idat.size(), &rawLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (!raw) { err = "PNG data could not be decompressed"; return false; }
    struct Free { void* p; ~Free() { std::free(p); } } guard{raw};
    if (rawLen < (stride + 1) * h) { err = "PNG data too short"; return false; }

    std::vector<uint8_t> img(stride * h);
    const uint8_t* src = static_cast<uint8_t*>(raw);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t ft = src[y * (stride + 1)];
        const uint8_t* in = src + y * (stride + 1) + 1;
        uint8_t* out = &img[y * stride];
        const uint8_t* prev = y ? &img[(y - 1) * stride] : nullptr;
        for (size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? out[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            int v = in[x];
            switch (ft) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: {
                    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: err = "bad PNG filter"; return false;
            }
            out[x] = uint8_t(v);
        }
    }

    width = int(w); height = int(h);
    rgba.assign(size_t(w) * h * 4, 255);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* row = &img[y * stride];
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* o = &rgba[(size_t(y) * w + x) * 4];
            switch (ctype) {
                case 0: o[0] = o[1] = o[2] = row[x]; break;
                case 2: o[0] = row[x * 3]; o[1] = row[x * 3 + 1]; o[2] = row[x * 3 + 2]; break;
                case 3: {
                    const size_t bit = size_t(x) * depth;
                    const unsigned idx = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1);
                    if (idx * 3 + 2 >= palette.size()) { err = "PNG palette index out of range"; return false; }
                    o[0] = palette[idx * 3]; o[1] = palette[idx * 3 + 1]; o[2] = palette[idx * 3 + 2];
                    if (idx < trns.size()) o[3] = trns[idx];
                    break;
                }
                case 4: o[0] = o[1] = o[2] = row[x * 2]; o[3] = row[x * 2 + 1]; break;
                case 6: std::memcpy(o, &row[x * 4], 4); break;
            }
        }
    }
    return true;
}

#include <filesystem>
#include <fstream>

bool loadPngPremultiplied(const std::string& utf8Path, int& width, int& height, std::vector<uint8_t>& rgba, std::string& err) {
    std::ifstream f(std::filesystem::u8path(utf8Path), std::ios::binary);
    if (!f) { err = "cannot open the file"; return false; }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!decodePng(file, width, height, rgba, err)) return false;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c) rgba[i + c] = uint8_t(rgba[i + c] * rgba[i + 3] / 255);
    return true;
}
