// Small command-line helper for patch mods (format: the game project's docs/formats/mod_patch.md, "step1").
//   step_patch_tool make <original> <modified> <out.step> [--raw]   writes a patch (prefix/suffix COPY + INSERT of the middle)
//   step_patch_tool check <original> <patch.step> <expected_result_file>   applies the patch and compares
//   step_patch_tool hash <file>                                    prints the SHA-256
//   step_patch_tool fuzz <original> <patch.step> <expected_result_file> [n]   mutates the patch n times (CRC fixed up) and checks the decoder never crashes
// Test and starter tool; it is not the planned size-optimised encoder.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "core/mod_patch.h"
#include "miniz.h"

namespace {
bool slurp(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "hash")) {
        std::string err;
        const std::string h = sha256OfFile(argv[2], err);
        if (h.empty()) { std::fprintf(stderr, "%s %s\n", argv[2], err.c_str()); return 1; }
        std::printf("sha256:%s\n", h.c_str());
        return 0;
    }
    if (argc >= 5 && !std::strcmp(argv[1], "make")) {
        std::vector<uint8_t> a, b;
        if (!slurp(argv[2], a) || !slurp(argv[3], b)) { std::fprintf(stderr, "cannot read input\n"); return 1; }
        const bool raw = argc >= 6 && !std::strcmp(argv[5], "--raw");
        const auto p = encodeStep1Simple(a, b, !raw);
        std::ofstream o(argv[4], std::ios::binary | std::ios::trunc);
        o.write(reinterpret_cast<const char*>(p.data()), std::streamsize(p.size()));
        std::printf("base_sha256   sha256:%s\nresult_sha256 sha256:%s\nresult size   %zu\npatch size    %zu\n", sha256OfBytes(a).c_str(),
                    sha256OfBytes(b).c_str(), b.size(), p.size());
        return o ? 0 : 1;
    }
    if (argc >= 5 && !std::strcmp(argv[1], "check")) {
        std::vector<uint8_t> a, p, want, out;
        if (!slurp(argv[2], a) || !slurp(argv[3], p) || !slurp(argv[4], want)) { std::fprintf(stderr, "cannot read input\n"); return 1; }
        PatchState st;
        std::string err;
        if (!applyStep1(p, a, sha256OfBytes(a), sha256OfBytes(want), out, st, err)) { std::fprintf(stderr, "FAILED: %s\n", err.c_str()); return 2; }
        std::printf("OK, %zu bytes\n", out.size());
        return 0;
    }
    if (argc >= 5 && !std::strcmp(argv[1], "fuzz")) {
        std::vector<uint8_t> a, p, want;
        if (!slurp(argv[2], a) || !slurp(argv[3], p) || !slurp(argv[4], want)) { std::fprintf(stderr, "cannot read input\n"); return 1; }
        const int n = argc >= 6 ? std::atoi(argv[5]) : 20000;
        std::mt19937 rng(12345);
        int accepted = 0, rejected = 0;
        const std::string hb = sha256OfBytes(a), hr = sha256OfBytes(want);
        for (int i = 0; i < n; ++i) {
            std::vector<uint8_t> q = p;
            const int edits = 1 + int(rng() % 4);
            for (int e = 0; e < edits; ++e) q[rng() % (q.size() - 4)] = uint8_t(rng());
            if (rng() % 8 == 0) q.resize(4 + rng() % q.size());  // truncate
            if (q.size() >= 4) {  // keep the checksum valid so the decoder's own checks are exercised
                const uint32_t c = uint32_t(mz_crc32(MZ_CRC32_INIT, q.data(), q.size() - 4));
                for (int k = 0; k < 4; ++k) q[q.size() - 4 + size_t(k)] = uint8_t(c >> (8 * k));
            }
            std::vector<uint8_t> out;
            PatchState st;
            std::string err;
            if (applyStep1(q, a, hb, hr, out, st, err)) ++accepted; else ++rejected;
        }
        std::printf("%d mutated patches: %d rejected, %d accepted (accepted ones produced the exact expected hash), no crash\n", n, rejected, accepted);
        return 0;
    }
    std::fprintf(stderr, "usage: step_patch_tool make|check|hash|fuzz ...\n");
    return 1;
}
