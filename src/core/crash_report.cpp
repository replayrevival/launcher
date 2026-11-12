#include "core/crash_report.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>

#include "core/identify.h"

namespace fs = std::filesystem;

namespace {

constexpr uintmax_t kMaxDump = 300u * 1024u * 1024u;  // bigger dumps are left out (the zip stays sendable)

bool readFile(const fs::path& p, std::string& out, uintmax_t limit = 20u * 1024u * 1024u) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size > limit) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string slashes(std::string s, char to) {
    for (auto& c : s) if (c == '/' || c == '\\') c = to;
    return s;
}

// Case-insensitive replace of every occurrence of `from`.
std::string replaceAll(const std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    const std::string lt = lowerAscii(text), lf = lowerAscii(from);
    std::string out;
    size_t pos = 0;
    for (;;) {
        const size_t hit = lt.find(lf, pos);
        if (hit == std::string::npos) break;
        out.append(text, pos, hit - pos);
        out += to;
        pos = hit + from.size();
    }
    out.append(text, pos, std::string::npos);
    return out;
}

}  // namespace

CrashEvidence findCrashEvidence(const fs::path& dumpDir, const fs::path& outLog, const fs::path& errLog, double maxAgeSeconds) {
    CrashEvidence ev;
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    fs::file_time_type best{};
    for (auto& e : fs::directory_iterator(dumpDir, ec)) {
        if (e.path().extension() != ".dmp") continue;
        std::error_code e2;
        const auto t = e.last_write_time(e2);
        if (e2) continue;
        if (std::chrono::duration<double>(now - t).count() > maxAgeSeconds) continue;
        if (ev.newestDump.empty() || t > best) { ev.newestDump = e.path(); best = t; }
    }
    for (const fs::path& log : {outLog, errLog}) {
        std::string text;
        if (!log.empty() && readFile(log, text) && text.find("[CRASH]") != std::string::npos) ev.crashLine = true;
    }
    return ev;
}

std::string redactPaths(const std::string& text, const std::vector<std::pair<std::string, std::string>>& replacements) {
    std::string out = text;
    // Longest paths first, so "D:\Games\STE\data" is replaced before "D:\Games\STE".
    auto rep = replacements;
    std::sort(rep.begin(), rep.end(), [](auto& a, auto& b) { return a.first.size() > b.first.size(); });
    for (auto& [from, to] : rep) {
        if (from.size() < 4) continue;  // never replace tiny strings like "C:\"
        std::string f = from;
        while (f.size() > 3 && (f.back() == '\\' || f.back() == '/')) f.pop_back();
        out = replaceAll(out, slashes(f, '\\'), to);
        out = replaceAll(out, slashes(f, '/'), slashes(to, '/'));
        // JSON / escaped form: C:\\Users\\name
        std::string esc;
        for (char c : slashes(f, '\\')) { esc += c; if (c == '\\') esc += '\\'; }
        out = replaceAll(out, esc, to);
    }
    // Any other user profile path.
    const std::string lo = lowerAscii(out);
    std::string res;
    size_t pos = 0;
    for (;;) {
        size_t hit = lo.find(":\\users\\", pos);
        size_t hit2 = lo.find(":/users/", pos);
        bool slash = false;
        if (hit == std::string::npos || (hit2 != std::string::npos && hit2 < hit)) { hit = hit2; slash = true; }
        if (hit == std::string::npos || hit == 0) break;
        const char sep = slash ? '/' : '\\';
        const size_t nameStart = hit + 8;
        size_t nameEnd = nameStart;
        while (nameEnd < out.size() && out[nameEnd] != sep && out[nameEnd] != '\\' && out[nameEnd] != '/' && out[nameEnd] != '"' &&
               out[nameEnd] != '\r' && out[nameEnd] != '\n' && out[nameEnd] != ' ' && out[nameEnd] != '\t')
            ++nameEnd;
        res.append(out, pos, nameStart - pos);
        res += "<user>";
        pos = nameEnd;
    }
    res.append(out, pos, std::string::npos);
    return res;
}

bool writeCrashReport(const CrashReportInput& in, std::string& err) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) { err = "The report could not be prepared (out of memory)."; return false; }
    auto add = [&](const char* name, const std::string& data) {
        return mz_zip_writer_add_mem(&zip, name, data.data(), data.size(), MZ_DEFAULT_LEVEL) != 0;
    };
    bool ok = true;
    std::string readme =
        "Crash report\r\n\r\n"
        "This zip was made by the launcher's \"Save crash report\" button. It contains:\r\n"
        "  info.txt            launcher and game version, exit code, graphics card, the settings of that start\r\n"
        "  game_output.txt     the game's normal log of that start\r\n"
        "  game_errors.txt     the game's error log of that start\r\n"
        "  ste_enhance.ini, ste_video.ini   your game settings\r\n"
        "  crash.dmp           a crash dump (a snapshot of the game's memory when it crashed), if the game wrote one\r\n\r\n"
        "In the text files your user name and folder names are replaced by <user>, <game folder> and similar. The crash dump is a memory\r\n"
        "snapshot and cannot be cleaned that way: it can contain file paths. It contains no game files you can use, but if you are\r\n"
        "not comfortable sending it, delete crash.dmp from the zip and send the rest.\r\n\r\n" + in.supportText + "\r\n";
    ok = ok && add("README.txt", readme);
    ok = ok && add("info.txt", redactPaths(in.info, in.redactions));
    std::string text;
    if (readFile(in.outLog, text)) ok = ok && add("game_output.txt", redactPaths(text, in.redactions));
    if (readFile(in.errLog, text)) ok = ok && add("game_errors.txt", redactPaths(text, in.redactions));
    for (const char* ini : {"ste_enhance.ini", "ste_video.ini"})
        if (readFile(in.settingsDir / ini, text)) ok = ok && add(ini, redactPaths(text, in.redactions));
    if (!in.evidence.newestDump.empty()) {
        std::string dump;
        if (readFile(in.evidence.newestDump, dump, kMaxDump)) ok = ok && add("crash.dmp", dump);
        else ok = ok && add("crash_dump_missing.txt", "The crash dump was too large or could not be read, so it was left out.\r\n");
    }
    void* buf = nullptr;
    size_t len = 0;
    ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buf, &len);
    if (!ok) {
        mz_zip_writer_end(&zip);
        err = "The report could not be put together.";
        return false;
    }
    std::error_code ec;
    struct Free { void* p; ~Free() { mz_free(p); } } freeBuf{buf};
    fs::create_directories(in.outZip.parent_path(), ec);
    {
        std::ofstream f(in.outZip, std::ios::binary | std::ios::trunc);
        if (f) f.write(static_cast<const char*>(buf), std::streamsize(len));
        if (!f) { mz_zip_writer_end(&zip); err = "The report could not be written to " + pathToUtf8(in.outZip) + ". Check that the folder exists and that you can write there."; return false; }
    }
    mz_zip_writer_end(&zip);
    return true;
}
