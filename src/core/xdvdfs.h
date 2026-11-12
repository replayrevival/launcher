#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Native read-only XDVDFS (Xbox disc) reader and unpacker. No external tools.
//   - The image is only ever opened for reading.
//   - Every name from the disc is validated before it is used (no "..", separators, drive letters,
//     reserved Windows names, duplicates); everything is written inside the target folder only.
//   - Unpacking goes into "<target>.partial" and is renamed to <target> only when it is complete,
//     so a cancelled or failed run never leaves something that looks like a valid game folder.

struct XdvdfsEntry {
    std::string path;      // relative to the disc root, UTF-8, '/' separated, already validated
    bool dir = false;
    uint64_t offset = 0;   // byte offset inside the image (files only)
    uint64_t size = 0;     // bytes (files only)
};

struct UnpackProgress {
    std::atomic<uint64_t> bytesDone{0}, bytesTotal{0};
    std::atomic<uint32_t> filesDone{0}, filesTotal{0};
    std::atomic<bool> cancel{false};
    std::atomic<uint32_t> throttleMs{0};   // testing only: sleep per 1 MiB chunk
};

struct UnpackResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;     // plain-language message when !ok && !cancelled
};

// Lists every file and folder of the game partition (validated). Used for the size estimate and by unpackImage().
bool xdvdfsList(const std::filesystem::path& image, std::vector<XdvdfsEntry>& out, uint64_t& totalBytes, std::string& err);

// Free space needed on top of the data itself (file system overhead, the savegame the game makes later is elsewhere).
constexpr uint64_t kUnpackSlackBytes = 64ull * 1024 * 1024;

// Empty string when unpacking `bytes` into `finalDir` is allowed; otherwise the plain-language reason (folder exists, would be placed in the
// disc image's own folder, not enough free space). LAUNCHER_FAKE_FREE_MB overrides the free space (testing only).
std::string unpackPreflight(const std::filesystem::path& image, const std::filesystem::path& finalDir, uint64_t bytes);

// Unpacks the image into `finalDir` (which must not exist yet). `finalDir`'s parent is created if needed.
UnpackResult unpackImage(const std::filesystem::path& image, const std::filesystem::path& finalDir, UnpackProgress& progress);

// Pure helpers (also used by the self test).
bool xdvdfsNameOk(const std::string& rawName, std::string& utf8, std::string& why);
