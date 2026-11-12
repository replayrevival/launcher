// Test tool for the disc-image unpacker (not shipped).
//   xiso_unpack_tool list   <image>
//   xiso_unpack_tool unpack <image> <target> [--cancel-after-bytes N] [--throttle-ms N]
//   xiso_unpack_tool compare <folderA> <folderB>      relative path + size + SHA-256 of every file, must match exactly
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "core/identify.h"
#include "core/sha256.h"
#include "core/xdvdfs.h"

namespace fs = std::filesystem;

static std::map<std::string, std::pair<uint64_t, std::string>> scan(const fs::path& root) {
    std::map<std::string, std::pair<uint64_t, std::string>> m;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto& e = *it;
        std::string rel = pathToUtf8(fs::relative(e.path(), root, ec));
        for (auto& c : rel) if (c == '\\') c = '/';
        if (e.is_directory(ec)) { m[rel + "/"] = {0, "dir"}; continue; }
        Sha256 sha;
        std::ifstream f(e.path(), std::ios::binary);
        std::vector<char> buf(1 << 20);
        uint64_t n = 0;
        while (f) { f.read(buf.data(), std::streamsize(buf.size())); const auto g = f.gcount(); if (g > 0) { sha.update(buf.data(), size_t(g)); n += uint64_t(g); } }
        m[rel] = {n, sha.finishHex()};
    }
    return m;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::puts("usage: xiso_unpack_tool list|unpack|compare ..."); return 2; }
    const std::string cmd = argv[1];
    if (cmd == "list") {
        std::vector<XdvdfsEntry> v; uint64_t total = 0; std::string err;
        if (!xdvdfsList(pathFromUtf8(argv[2]), v, total, err)) { std::printf("ERROR: %s\n", err.c_str()); return 1; }
        size_t files = 0, dirs = 0;
        for (auto& e : v) (e.dir ? dirs : files)++;
        std::printf("OK: %zu files, %zu folders, %llu bytes\n", files, dirs, (unsigned long long)total);
        return 0;
    }
    if (cmd == "unpack" && argc >= 4) {
        UnpackProgress pr;
        uint64_t cancelAfter = 0;
        for (int i = 4; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--cancel-after-bytes")) cancelAfter = std::strtoull(argv[i + 1], nullptr, 10);
            if (!std::strcmp(argv[i], "--throttle-ms")) pr.throttleMs = uint32_t(std::atoi(argv[i + 1]));
        }
        UnpackResult res;
        std::atomic<bool> done{false};
        std::thread t([&] { res = unpackImage(pathFromUtf8(argv[2]), pathFromUtf8(argv[3]), pr); done = true; });
        while (!done) {
            if (cancelAfter && pr.bytesDone >= cancelAfter) pr.cancel = true;
            if (pr.bytesTotal) std::fprintf(stderr, "\r%u/%u files  %llu/%llu bytes", pr.filesDone.load(), pr.filesTotal.load(), (unsigned long long)pr.bytesDone.load(), (unsigned long long)pr.bytesTotal.load());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        t.join();
        std::fprintf(stderr, "\n");
        std::printf("%s%s%s\n", res.ok ? "OK" : (res.cancelled ? "CANCELLED" : "ERROR: "), res.ok ? "" : "", res.error.c_str());
        return res.ok ? 0 : (res.cancelled ? 3 : 1);
    }
    if (cmd == "compare" && argc >= 4) {
        auto a = scan(pathFromUtf8(argv[2])), b = scan(pathFromUtf8(argv[3]));
        size_t diff = 0, files = 0; uint64_t bytes = 0;
        for (auto& [k, v] : a) {
            auto it = b.find(k);
            if (it == b.end()) { std::printf("ONLY IN A: %s\n", k.c_str()); ++diff; continue; }
            if (it->second != v) { std::printf("DIFFERENT: %s (%llu vs %llu bytes)\n", k.c_str(), (unsigned long long)v.first, (unsigned long long)it->second.first); ++diff; }
            if (k.back() != '/') { ++files; bytes += v.first; }
        }
        for (auto& [k, v] : b) if (!a.count(k)) { std::printf("ONLY IN B: %s\n", k.c_str()); ++diff; }
        std::printf("%s: %zu files compared (%llu bytes), %zu entries in A, %zu in B, %zu differences\n", diff ? "MISMATCH" : "IDENTICAL", files, (unsigned long long)bytes, a.size(), b.size(), diff);
        return diff ? 1 : 0;
    }
    return 2;
}
