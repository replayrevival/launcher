#include "core/mod_patch.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "core/identify.h"  // pathFromUtf8 / pathToUtf8
#include "core/sha256.h"
#include "miniz.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr uint64_t kMaxFile = 1900ull << 20;  // one game file or result: 1.9 GB at most (it is held in memory)
constexpr size_t kHeader = 96;

FILE* openFile(const fs::path& p, const char* mode) {
#ifdef _WIN32
    const std::wstring m(mode, mode + std::char_traits<char>::length(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

bool readAll(const fs::path& p, std::vector<uint8_t>& out, std::string& err) {
    std::error_code ec;
    const uint64_t size = fs::file_size(p, ec);
    if (ec) { err = "cannot be read"; return false; }
    if (size > kMaxFile) { err = "is too large to patch"; return false; }
    FILE* f = openFile(p, "rb");
    if (!f) { err = "cannot be opened"; return false; }
    out.resize(size_t(size));
    const size_t n = size ? std::fread(out.data(), 1, size_t(size), f) : 0;
    std::fclose(f);
    if (n != size) { err = "could not be read completely"; return false; }
    return true;
}

bool writeAll(const fs::path& p, const std::vector<uint8_t>& data, std::string& err) {
    FILE* f = openFile(p, "wb");
    if (!f) { err = "cannot create " + pathToUtf8(p); return false; }
    const size_t n = data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), f);
    const bool ok = std::fclose(f) == 0 && n == data.size();
    if (!ok) err = "could not write " + pathToUtf8(p);
    return ok;
}

std::string hexRaw(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

std::string shortHash(const std::string& h) { return h.size() > 12 ? "sha256:" + h.substr(0, 12) + "..." : "sha256:" + h; }

uint64_t le64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct Reader {
    const uint8_t* p;
    size_t n, i = 0;
    bool byte(uint8_t& b) { if (i >= n) return false; b = p[i++]; return true; }
    bool varint(uint64_t& v) {  // LEB128, at most 10 bytes
        v = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            uint8_t b;
            if (!byte(b)) return false;
            if (shift == 63 && b > 1) return false;
            v |= uint64_t(b & 0x7F) << shift;
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    bool skip(uint64_t len, const uint8_t*& at) {
        if (len > n - i) return false;
        at = p + i;
        i += size_t(len);
        return true;
    }
};

void putVar(std::vector<uint8_t>& o, uint64_t v) {
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v) b |= 0x80;
        o.push_back(b);
    } while (v);
}
void put64(std::vector<uint8_t>& o, uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void hexToRaw(const std::string& hex, std::vector<uint8_t>& o) {
    for (size_t i = 0; i + 1 < hex.size(); i += 2) o.push_back(uint8_t(std::stoi(hex.substr(i, 2), nullptr, 16)));
}

}  // namespace

std::string sha256OfBytes(const std::vector<uint8_t>& b) {
    Sha256 h;
    h.update(b.data(), b.size());
    return h.finishHex();
}

std::string sha256OfFile(const fs::path& p, std::string& err) {
    FILE* f = openFile(p, "rb");
    if (!f) { err = "cannot be opened"; return {}; }
    Sha256 h;
    std::vector<uint8_t> buf(1 << 20);
    for (;;) {
        const size_t n = std::fread(buf.data(), 1, buf.size(), f);
        if (n == 0) break;
        h.update(buf.data(), n);
    }
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) { err = "could not be read completely"; return {}; }
    return h.finishHex();
}

// ---------------------------------------------------------------- step1 decoder

bool applyStep1(const std::vector<uint8_t>& patch, const std::vector<uint8_t>& original, const std::string& expectBase,
                const std::string& expectResult, std::vector<uint8_t>& out, PatchState& failState, std::string& err) {
    failState = PatchState::BadPatch;
    out.clear();
    if (patch.size() < kHeader + 4 || std::memcmp(patch.data(), "STEPATC1", 8) != 0) { err = "The patch file is not a step1 patch."; return false; }
    const uint32_t crc = le32(patch.data() + patch.size() - 4);
    if (uint32_t(mz_crc32(MZ_CRC32_INIT, patch.data(), patch.size() - 4)) != crc) { err = "The patch file is damaged (checksum)."; return false; }

    const uint64_t baseSize = le64(patch.data() + 8), resultSize = le64(patch.data() + 16);
    const std::string hBase = hexRaw(patch.data() + 24, 32), hResult = hexRaw(patch.data() + 56, 32);
    const uint32_t opCount = le32(patch.data() + 88), flags = le32(patch.data() + 92);
    if (flags & ~1u) { err = "The patch uses features this launcher does not know."; return false; }
    if (hBase != expectBase || hResult != expectResult) { err = "The patch was made for different files than the manifest says."; return false; }
    if (baseSize != original.size()) { err = "The file's size does not match what the patch expects."; return false; }
    if (resultSize > kMaxFile) { err = "The patched file would be too large."; return false; }

    // Op stream (optionally zlib). The decompressed size is bounded so a hostile patch cannot use up memory.
    const uint8_t* ops = patch.data() + kHeader;
    size_t opsLen = patch.size() - kHeader - 4;
    std::vector<uint8_t> inflated;
    if (flags & 1u) {
        const uint64_t cap = resultSize + uint64_t(opCount) * 32 + 64;
        if (cap > kMaxFile + (64ull << 20)) { err = "The patch is too large."; return false; }
        inflated.resize(size_t(cap));
        tinfl_decompressor d;
        tinfl_init(&d);
        size_t inLen = opsLen, outLen = inflated.size();
        const tinfl_status st = tinfl_decompress(&d, ops, &inLen, inflated.data(), inflated.data(), &outLen,
                                                 TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (st != TINFL_STATUS_DONE) { err = "The patch's data is damaged or too large."; return false; }
        inflated.resize(outLen);
        ops = inflated.data();
        opsLen = inflated.size();
    }

    out.reserve(size_t(resultSize));
    Reader r{ops, opsLen};
    for (uint32_t k = 0; k < opCount; ++k) {
        uint8_t kind;
        if (!r.byte(kind)) { err = "The patch ends too early."; out.clear(); return false; }
        uint64_t a = 0, b = 0;
        if (kind == 0 || kind == 1) {  // COPY / ADD: src_off, len
            if (!r.varint(a) || !r.varint(b) || a > original.size() || b > original.size() - a || b > resultSize - out.size()) {
                err = "The patch refers to bytes outside the file.";
                out.clear();
                return false;
            }
            if (kind == 0) {
                out.insert(out.end(), original.begin() + ptrdiff_t(a), original.begin() + ptrdiff_t(a + b));
            } else {
                const uint8_t* delta;
                if (!r.skip(b, delta)) { err = "The patch ends too early."; out.clear(); return false; }
                for (uint64_t i = 0; i < b; ++i) out.push_back(uint8_t(original[size_t(a + i)] + delta[i]));
            }
        } else if (kind == 2) {  // INSERT: len, literal bytes
            const uint8_t* lit;
            if (!r.varint(b) || b > resultSize - out.size() || !r.skip(b, lit)) { err = "The patch ends too early or is too long."; out.clear(); return false; }
            out.insert(out.end(), lit, lit + b);
        } else {
            err = "The patch has an unknown step.";
            out.clear();
            return false;
        }
    }
    if (r.i != r.n || out.size() != resultSize) { err = "The patch does not produce the size it announces."; out.clear(); return false; }
    if (sha256OfBytes(out) != expectResult) {
        failState = PatchState::ResultMismatch;
        err = "The patched file does not match the expected hash; it was discarded.";
        out.clear();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- simple encoder (tests, small modders' tools)

std::vector<uint8_t> encodeStep1Simple(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, bool compress) {
    size_t pre = 0;
    while (pre < a.size() && pre < b.size() && a[pre] == b[pre]) ++pre;
    size_t suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre && a[a.size() - 1 - suf] == b[b.size() - 1 - suf]) ++suf;
    std::vector<uint8_t> ops;
    uint32_t count = 0;
    if (pre) { ops.push_back(0); putVar(ops, 0); putVar(ops, pre); ++count; }
    const size_t midB = b.size() - pre - suf;
    if (midB) { ops.push_back(2); putVar(ops, midB); ops.insert(ops.end(), b.begin() + ptrdiff_t(pre), b.begin() + ptrdiff_t(pre + midB)); ++count; }
    if (suf) { ops.push_back(0); putVar(ops, a.size() - suf); putVar(ops, suf); ++count; }

    std::vector<uint8_t> o;
    const char magic[] = "STEPATC1";
    o.insert(o.end(), magic, magic + 8);
    put64(o, a.size());
    put64(o, b.size());
    hexToRaw(sha256OfBytes(a), o);
    hexToRaw(sha256OfBytes(b), o);
    put32(o, count);
    put32(o, compress ? 1u : 0u);
    if (compress) {
        size_t n = 0;
        void* z = tdefl_compress_mem_to_heap(ops.data(), ops.size(), &n, TDEFL_DEFAULT_MAX_PROBES | TDEFL_WRITE_ZLIB_HEADER);
        if (z) { o.insert(o.end(), static_cast<uint8_t*>(z), static_cast<uint8_t*>(z) + n); mz_free(z); }
    } else {
        o.insert(o.end(), ops.begin(), ops.end());
    }
    put32(o, uint32_t(mz_crc32(MZ_CRC32_INIT, o.data(), o.size())));
    return o;
}

// ---------------------------------------------------------------- state and preparing

namespace {

struct BuiltEntry {
    std::string base, result, patch, tool;
    uint64_t size = 0, baseSize = 0;
    long long baseMtime = 0;
};

long long mtimeOf(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : (long long)t.time_since_epoch().count();
}

fs::path builtDir(const ModInfo& m) { return m.dir / "_built"; }
fs::path builtPath(const ModInfo& m, const std::string& target) { return builtDir(m) / pathFromUtf8(target); }

std::map<std::string, BuiltEntry> loadState(const ModInfo& m) {
    std::map<std::string, BuiltEntry> st;
    try {
        std::ifstream f(builtDir(m) / "state.json", std::ios::binary);
        if (!f) return st;
        const json j = json::parse(f);
        if (!j.contains("entries") || !j["entries"].is_object()) return st;
        for (auto it = j["entries"].begin(); it != j["entries"].end(); ++it) {
            const json& e = it.value();
            BuiltEntry b;
            auto s = [&](const char* k) { return e.contains(k) && e[k].is_string() ? e[k].get<std::string>() : std::string(); };
            b.base = s("base_sha256"); b.result = s("result_sha256"); b.patch = s("patch_sha256"); b.tool = s("tool");
            if (e.contains("size") && e["size"].is_number_unsigned()) b.size = e["size"].get<uint64_t>();
            if (e.contains("base_size") && e["base_size"].is_number_unsigned()) b.baseSize = e["base_size"].get<uint64_t>();
            if (e.contains("base_mtime") && e["base_mtime"].is_number_integer()) b.baseMtime = e["base_mtime"].get<long long>();
            st[it.key()] = b;
        }
    } catch (...) { st.clear(); }
    return st;
}

bool saveState(const ModInfo& m, const std::map<std::string, BuiltEntry>& st, std::string& err) {
    json j;
    j["tool"] = kPatchToolName;
    j["entries"] = json::object();
    for (auto& [k, b] : st)
        j["entries"][k] = {{"base_sha256", b.base}, {"result_sha256", b.result}, {"patch_sha256", b.patch}, {"tool", b.tool},
                           {"size", b.size}, {"base_size", b.baseSize}, {"base_mtime", b.baseMtime}};
    std::error_code ec;
    fs::create_directories(builtDir(m), ec);
    const fs::path tmp = builtDir(m) / "state.json.part", dst = builtDir(m) / "state.json";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(2);
        if (!f) { err = "could not write state.json"; return false; }
    }
    fs::rename(tmp, dst, ec);
    if (ec) { err = "could not write state.json"; return false; }
    return true;
}

std::string lowerKey(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool isExternalFormat(const std::string& f) { return f == "bsf-edit" || f == "zip-of" || f == "text-edit" || f == "builtin"; }

std::string normTarget(std::string t) {
    for (auto& c : t) { if (c == '\\') c = '/'; c = char(std::tolower((unsigned char)c)); }
    return t;
}

bool usesStack(const ModInfo& m) {
    bool ext = false;
    for (auto& p : m.patches) {
        if (!isExternalFormat(p.format)) return false;
        if (p.format == "zip-of") return false;  // zip rebuilds go through `prepare` on their own
        ext = true;
    }
    return ext;
}

// The mods that build one file on top of each other, in load order, up to and including `m`.
std::vector<std::string> expectedStack(const ModStore* store, const ModInfo& m, const std::string& target,
                                       const std::function<bool(const ModInfo&)>* fits) {
    std::vector<std::string> ids;
    if (!store) { ids.push_back(m.id); return ids; }
    const std::string key = normTarget(target);
    for (const ModInfo& o : store->mods()) {
        if (!usesStack(o) || !o.enabled || !o.problem.empty()) continue;
        if (fits && !(*fits)(o)) continue;
        bool has = false;
        for (auto& q : o.patches) if (normTarget(q.target) == key) has = true;
        if (!has) continue;
        ids.push_back(o.id);
        if (o.id == m.id) break;
    }
    return ids;
}

// Is the built file there and the one the manifest announces (alone), or the one Prepare recorded for exactly this stack of mods?
bool builtMatches(const ModInfo& m, const ModPatchEntry& p, const std::vector<std::string>& expected) {
    std::error_code ec;
    const fs::path b = builtPath(m, p.target);
    if (!fs::is_regular_file(b, ec)) return false;
    if (expected.size() <= 1) {
        if (p.size && fs::file_size(b, ec) != p.size) return false;
        std::string e;
        return !p.resultSha256.empty() && sha256OfFile(b, e) == p.resultSha256;
    }
    std::string e;
    const std::string sha = sha256OfFile(b, e);
    try {
        std::ifstream f(builtDir(m) / "_stack.json", std::ios::binary);
        if (!f) return false;
        const json j = json::parse(f);
        const std::string want = normTarget(p.target);
        for (auto it = j.at("targets").begin(); it != j.at("targets").end(); ++it) {
            if (normTarget(it.key()) != want) continue;
            if (it.value().at("sha256").get<std::string>() != sha) return false;
            return it.value().at("stack").get<std::vector<std::string>>() == expected;
        }
        return false;
    } catch (...) { return false; }
}

}  // namespace

bool isExternalPatch(const ModPatchEntry& p) { return isExternalFormat(p.format); }
bool modNeedsExternalTool(const ModInfo& m) {
    for (auto& p : m.patches) if (isExternalPatch(p)) return true;
    return false;
}

namespace {

}  // namespace

std::vector<ModPatchReport> quickPatchStates(const ModInfo& mod, const fs::path& gameDir, const ModStore* store) {
    std::vector<ModPatchReport> out;
    if (mod.patches.empty()) return out;
    const auto st = loadState(mod);
    const bool ext = modNeedsExternalTool(mod);
    for (auto& p : mod.patches) {
        ModPatchReport r;
        r.modId = mod.id;
        r.target = p.target;
        if (ext) {  // built by the helper tool: current when the built file is there and has the announced hash
            r.state = builtMatches(mod, p, usesStack(mod) ? expectedStack(store, mod, p.target, nullptr) : std::vector<std::string>{mod.id}) ? PatchState::Ready : PatchState::NotBuilt;
            out.push_back(std::move(r));
            continue;
        }
        std::string e;
        const std::string patchSha = sha256OfFile(mod.dir / pathFromUtf8(p.patchRel), e);
        const auto it = st.find(p.target);
        std::error_code ec;
        const fs::path built = builtPath(mod, p.target), orig = gameDir / pathFromUtf8(p.target);
        const bool current = it != st.end() && !patchSha.empty() && it->second.base == p.baseSha256 && it->second.result == p.resultSha256 &&
                             it->second.patch == patchSha && it->second.tool == kPatchToolName && fs::is_regular_file(built, ec) &&
                             fs::file_size(built, ec) == it->second.size && fs::is_regular_file(orig, ec) && fs::file_size(orig, ec) == it->second.baseSize &&
                             mtimeOf(orig) == it->second.baseMtime;
        r.state = current ? PatchState::Ready : PatchState::NotBuilt;
        out.push_back(std::move(r));
    }
    return out;
}

namespace {

using ToolLines = std::map<std::string, std::pair<PatchState, std::string>>;

// Reads the tool's output: `<target>: <result>` per file (prepare) or `<mod id>: <target>: <result>` / `CONFLICT <mod id>: <target>: <why>` (prepare-stack).
ToolLines parseToolLines(const std::string& output, bool stacked) {
    ToolLines lines;
    size_t pos = 0;
    while (pos <= output.size()) {
        const size_t nl = output.find('\n', pos);
        std::string line = output.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? output.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        bool conflict = false;
        if (line.rfind("CONFLICT ", 0) == 0) { conflict = true; line = line.substr(9); }
        std::string id;
        if (stacked) {
            const size_t c0 = line.find(": ");
            if (c0 == std::string::npos) continue;
            id = line.substr(0, c0) + "|";
            line = line.substr(c0 + 2);
        }
        const size_t c = line.find(": ");
        if (c == std::string::npos) continue;
        const std::string key = id + normTarget(line.substr(0, c)), msg = line.substr(c + 2);
        PatchState st;
        if (conflict) st = PatchState::BadPatch;
        else if (msg.rfind("built", 0) == 0) st = PatchState::Built;
        else if (msg.rfind("BASE MISMATCH", 0) == 0) st = PatchState::BaseMismatch;
        else if (msg.rfind("RESULT MISMATCH", 0) == 0) st = PatchState::ResultMismatch;
        else if (msg.find("not in your game") != std::string::npos) st = PatchState::MissingTarget;
        else st = PatchState::BadPatch;
        lines[key] = {st, conflict ? "CONFLICT: " + msg : msg};
    }
    return lines;
}

std::string lastLine(std::string o) {
    while (!o.empty() && (o.back() == '\n' || o.back() == '\r' || o.back() == ' ')) o.pop_back();
    const size_t nl = o.find_last_of('\n');
    std::string tail = o.substr(nl == std::string::npos ? 0 : nl + 1);
    if (tail.size() > 160) tail = tail.substr(0, 160) + "...";
    return tail;
}

}  // namespace

PatchRunResult preparePatches(const ModStore& store, const fs::path& gameDir, const std::function<bool(const ModInfo&)>& fits,
                              const std::atomic<bool>* cancel, const ExternalRunner& external, const StackRunner& stackRunner) {
    PatchRunResult run;
    bool stackRan = false;
    ExternalResult stackResult;
    ToolLines stackLines;
    struct Cur { fs::path path; std::string sha, owner; };
    std::map<std::string, Cur> cur;  // target -> where the current version of that file is

    for (const ModInfo& m : store.mods()) {
        if (m.patches.empty()) continue;
        auto add = [&](const ModPatchEntry& p, PatchState s, const std::string& detail) {
            ModPatchReport r;
            r.modId = m.id; r.target = p.target; r.state = s; r.detail = detail;
            run.reports.push_back(std::move(r));
            if (s == PatchState::Built) ++run.built;
            else if (s == PatchState::Ready) ++run.ready;
            else if (s != PatchState::Skipped) ++run.failed;
        };
        if (!m.problem.empty() || !m.enabled || !fits(m)) {
            for (auto& p : m.patches) add(p, PatchState::Skipped, m.enabled ? "This mod does not fit your game." : "The mod is switched off.");
            continue;
        }
        if (modNeedsExternalTool(m)) {
            const bool cancelled = cancel && cancel->load();
            const bool stacked = usesStack(m);
            std::map<std::string, std::vector<std::string>> exp;  // target -> mods stacked on it up to this one
            bool allReady = true;
            for (auto& p : m.patches) {
                exp[p.target] = stacked ? expectedStack(&store, m, p.target, &fits) : std::vector<std::string>{m.id};
                if (!builtMatches(m, p, exp[p.target])) allReady = false;
            }
            if (cancelled) { for (auto& p : m.patches) add(p, PatchState::Skipped, "Cancelled."); continue; }
            if (allReady) {
                for (auto& p : m.patches) { add(p, PatchState::Ready, ""); cur[lowerKey(p.target)] = {builtPath(m, p.target), p.resultSha256, m.id}; }
                continue;
            }
            auto failAll = [&](PatchState st, const std::string& why) { for (auto& p : m.patches) add(p, st, why); };
            ExternalResult er;
            ToolLines lines;
            if (stacked) {
                if (!stackRunner) { failAll(PatchState::BadPatch, "This mod is built on your PC and needs a helper that this launcher does not have."); continue; }
                if (!stackRan) {  // one run builds every enabled recipe mod, each on top of the earlier ones
                    stackRan = true;
                    std::vector<std::string> ids;
                    for (const ModInfo& o : store.mods()) if (usesStack(o) && o.enabled && o.problem.empty() && fits(o)) ids.push_back(o.id);
                    stackResult = stackRunner(store.root(), gameDir, ids);
                    if (stackResult.started) stackLines = parseToolLines(stackResult.output, true);
                }
                er = stackResult;
                lines = stackLines;
            } else {
                if (!external) { failAll(PatchState::BadPatch, "This mod is built on your PC and needs a helper that this launcher does not have."); continue; }
                er = external(m.dir, gameDir);
                if (er.started) lines = parseToolLines(er.output, false);
            }
            if (!er.started) { failAll(PatchState::BadPatch, er.startError.empty() ? "The helper that builds this mod could not be started." : er.startError); continue; }
            for (auto& p : m.patches) {
                const auto it = lines.find((stacked ? m.id + "|" : std::string()) + normTarget(p.target));
                if (it == lines.end()) {
                    const std::string tail = lastLine(er.output);
                    add(p, PatchState::BadPatch, er.exitCode == 0 ? "The helper did not report this file." :
                        "The helper stopped before it reached this file (exit code " + std::to_string(er.exitCode) + ")." + (tail.empty() ? "" : " It said: " + tail));
                } else if (it->second.first == PatchState::Built) {
                    if (builtMatches(m, p, exp[p.target])) { add(p, PatchState::Built, ""); cur[lowerKey(p.target)] = {builtPath(m, p.target), p.resultSha256, m.id}; }
                    else add(p, PatchState::ResultMismatch, "The built file is not the one this mod expects, so the game will use your original file.");
                } else {
                    std::string txt = it->second.second;
                    if (it->second.first == PatchState::BaseMismatch) txt = "Your game file is not the one this mod was made for (a different version of the game?).";
                    else if (it->second.first == PatchState::MissingTarget) txt = "Your game has no file \"" + p.target + "\".";
                    else if (it->second.first == PatchState::ResultMismatch) txt = "The rebuilt file does not match what the mod expects, so the game will use your original file.";
                    else if (txt.rfind("CONFLICT: ", 0) == 0) txt = "This mod changes the same thing as another mod you switched on (" + txt.substr(10) + "). It was left out, so the game uses your original file. Switch one of them off.";
                    add(p, it->second.first, txt);
                }
            }
            continue;
        }
        auto state = loadState(m);
        bool dirty = false, modFailed = false;
        std::map<std::string, Cur> pending;  // this mod's results are only visible to later mods once all its patches worked
        for (auto& p : m.patches) {
            if (cancel && cancel->load()) { add(p, PatchState::Skipped, "Cancelled."); continue; }
            if (modFailed) { add(p, PatchState::Skipped, "An earlier patch of this mod failed."); continue; }
            const std::string key = lowerKey(p.target);
            std::string err;
            if (p.format != "step1") { modFailed = true; add(p, PatchState::BadPatch, "Patch format \"" + p.format + "\" is not supported by this launcher."); continue; }

            // Which file does this patch build on: the player's original or an earlier mod's result?
            Cur base;
            const auto cit = cur.find(key);
            const auto pit = pending.find(key);
            if (pit != pending.end()) base = pit->second;
            else if (cit != cur.end()) base = cit->second;
            const fs::path orig = gameDir / pathFromUtf8(p.target);
            std::error_code ec;
            if (base.owner.empty()) {
                if (!fs::is_regular_file(orig, ec)) { modFailed = true; add(p, PatchState::MissingTarget, "Your game has no file \"" + p.target + "\"."); continue; }
                base.path = orig;
            }

            // Already built and current? (checked against the cached state, no hashing of the game file)
            const fs::path outPath = builtPath(m, p.target);
            const std::string patchSha = sha256OfFile(m.dir / pathFromUtf8(p.patchRel), err);
            if (patchSha.empty()) { modFailed = true; add(p, PatchState::BadPatch, "The patch file \"" + p.patchRel + "\" " + err + "."); continue; }
            if (!p.patchSha256.empty() && p.patchSha256 != patchSha) { modFailed = true; add(p, PatchState::BadPatch, "The patch file does not match its hash in the manifest."); continue; }
            const auto sit = state.find(p.target);
            const bool baseIsOriginal = base.owner.empty();
            if (sit != state.end() && sit->second.base == p.baseSha256 && sit->second.result == p.resultSha256 && sit->second.patch == patchSha &&
                sit->second.tool == kPatchToolName && fs::is_regular_file(outPath, ec) && fs::file_size(outPath, ec) == sit->second.size &&
                (!baseIsOriginal || (fs::file_size(orig, ec) == sit->second.baseSize && mtimeOf(orig) == sit->second.baseMtime)) &&
                (baseIsOriginal || base.sha == p.baseSha256)) {
                pending[key] = {outPath, p.resultSha256, m.id};
                add(p, PatchState::Ready, "");
                continue;
            }

            // Check the base, then build.
            std::string baseSha = base.sha;
            if (baseSha.empty()) {
                baseSha = sha256OfFile(base.path, err);
                if (baseSha.empty()) { modFailed = true; add(p, PatchState::MissingTarget, "\"" + p.target + "\" " + err + "."); continue; }
            }
            if (baseSha != p.baseSha256) {
                modFailed = true;
                if (!baseIsOriginal)
                    add(p, PatchState::Conflict, "Another mod (" + base.owner + ") already changes this file, and this mod was not made to build on its result.");
                else
                    add(p, PatchState::BaseMismatch, "Your file is " + shortHash(baseSha) + ", the patch needs " + shortHash(p.baseSha256) + ". It was made for a different version of the game.");
                continue;
            }
            std::vector<uint8_t> baseBytes, patchBytes, result;
            if (!readAll(base.path, baseBytes, err)) { modFailed = true; add(p, PatchState::MissingTarget, "\"" + p.target + "\" " + err + "."); continue; }
            if (!readAll(m.dir / pathFromUtf8(p.patchRel), patchBytes, err)) { modFailed = true; add(p, PatchState::BadPatch, "The patch file " + err + "."); continue; }
            PatchState fs_ = PatchState::BadPatch;
            if (!applyStep1(patchBytes, baseBytes, p.baseSha256, p.resultSha256, result, fs_, err)) { modFailed = true; add(p, fs_, err); continue; }
            if (p.size && result.size() != p.size) { modFailed = true; add(p, PatchState::ResultMismatch, "The patched file's size is not the size the manifest announces."); continue; }
            baseBytes.clear();
            baseBytes.shrink_to_fit();

            fs::create_directories(outPath.parent_path(), ec);
            const fs::path tmp = fs::path(outPath).concat(".part");
            if (!writeAll(tmp, result, err)) { modFailed = true; add(p, PatchState::BadPatch, err); continue; }
            fs::rename(tmp, outPath, ec);
            if (ec) { std::error_code e2; fs::remove(tmp, e2); modFailed = true; add(p, PatchState::BadPatch, "Could not place the built file: " + ec.message()); continue; }

            BuiltEntry b;
            b.base = p.baseSha256; b.result = p.resultSha256; b.patch = patchSha; b.tool = kPatchToolName; b.size = result.size();
            if (baseIsOriginal) { b.baseSize = fs::file_size(orig, ec); b.baseMtime = mtimeOf(orig); }
            state[p.target] = b;
            dirty = true;
            pending[key] = {outPath, p.resultSha256, m.id};
            add(p, PatchState::Built, "");
        }
        if (dirty) { std::string e; saveState(m, state, e); }
        if (!modFailed) for (auto& [k, v] : pending) cur[k] = v;
    }
    return run;
}
