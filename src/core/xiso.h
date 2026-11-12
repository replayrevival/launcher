#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Read-only reader for Xbox disc images (XISO). Finds files in the root directory of the
// game partition. Handles raw redumps (partition at 0) and the XGD1/XGD2/XGD3 layouts.
class XisoImage {
public:
    bool open(const std::filesystem::path& path);
    bool isXbox() const { return base_ != kNone; }
    // Looks up a root-directory entry (case-insensitive). Returns false if absent.
    bool findRootFile(const std::string& name, uint64_t& offset, uint32_t& size);
    bool read(uint64_t offset, void* buf, size_t n);

private:
    static constexpr uint64_t kNone = ~0ull;
    bool visit(const std::vector<uint8_t>& dir, uint32_t pos, const std::string& want, uint64_t& off, uint32_t& size, int depth);
    std::ifstream f_;
    uint64_t base_ = kNone;   // start of the game partition inside the file
    uint32_t rootSector_ = 0, rootSize_ = 0;
};
