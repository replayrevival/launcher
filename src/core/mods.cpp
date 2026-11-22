#include "core/mods.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/identify.h"  // pathFromUtf8 / pathToUtf8
#include "miniz.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

FILE* openFile(const fs::path& p, const char* mode) {
#ifdef _WIN32
    const std::wstring m(mode, mode + std::char_traits<char>::length(mode));
    return _wfopen(p.c_str(), m.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

// sha256:ABC... or ABC... -> lower-case hex; anything that is not 64 hex digits gives an empty string.
std::string hexOf(std::string s) {
    if (s.rfind("sha256:", 0) == 0) s.erase(0, 7);
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    if (s.size() != 64 || s.find_first_not_of("0123456789abcdef") != std::string::npos) return {};
    return s;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 80 || id[0] == '.' || id[0] == '_') return false;
    for (unsigned char c : id)
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_')) return false;
    return true;
}

std::string stamp() {
    char b[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", std::localtime(&t));
    return b;
}

std::vector<std::string> strList(const json& j, const char* key) {
    std::vector<std::string> out;
    if (j.contains(key) && j[key].is_array())
        for (auto& v : j[key])
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

// A relative path inside a mod: no "..", no drive or absolute part, no empty parts.
bool safeRel(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos || rel.find('\0') != std::string::npos) return false;
    size_t pos = 0;
    while (pos <= rel.size()) {
        size_t sl = rel.find('/', pos);
        if (sl == std::string::npos) sl = rel.size();
        const std::string part = rel.substr(pos, sl - pos);
        if (part.empty() || part == "." || part == "..") return false;
        pos = sl + 1;
    }
    return true;
}

bool parseManifest(const std::string& text, ModInfo& m, std::string& err) {
    try {
        const json j = json::parse(text);
        if (!j.is_object()) { err = "manifest.json is not a JSON object."; return false; }
        if (!j.contains("id") || !j["id"].is_string()) { err = "manifest.json has no \"id\"."; return false; }
        m.id = j["id"].get<std::string>();
        if (!validId(m.id)) { err = "The mod's id \"" + m.id + "\" is not valid (letters, digits, . - _ only)."; return false; }
        auto str = [&](const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); };
        m.name = str("name");
        m.version = str("version");
        m.description = str("description");
        m.game = str("game");
        m.profile = str("profile");
        m.license = str("license");
        m.authors = strList(j, "authors");
        m.gameHashes = strList(j, "game_hashes");
        m.dependencies = strList(j, "dependencies");
        m.optionalDependencies = strList(j, "optional_dependencies");
        m.conflicts = strList(j, "conflicts");
        if (j.contains("priority") && j["priority"].is_number()) m.priority = j["priority"].get<int>();
        if (j.contains("api") && j["api"].is_number()) m.api = j["api"].get<int>();
        if (j.contains("enabled") && j["enabled"].is_boolean()) m.enabledByManifest = j["enabled"].get<bool>();
        if (j.contains("patches")) {
            if (!j["patches"].is_array()) { err = "\"patches\" in manifest.json must be a list."; return false; }
            for (auto& p : j["patches"]) {
                ModPatchEntry e;
                auto ps = [&](const char* k) { return p.is_object() && p.contains(k) && p[k].is_string() ? p[k].get<std::string>() : std::string(); };
                e.target = ps("target");
                e.patchRel = ps("patch");
                e.baseSha256 = hexOf(ps("base_sha256"));
                e.resultSha256 = hexOf(ps("result_sha256"));
                e.patchSha256 = hexOf(ps("patch_sha256"));
                if (!ps("format").empty()) e.format = ps("format");
                if (p.is_object() && p.contains("size") && p["size"].is_number_unsigned()) e.size = p["size"].get<unsigned long long>();
                std::replace(e.target.begin(), e.target.end(), '\\', '/');
                std::replace(e.patchRel.begin(), e.patchRel.end(), '\\', '/');
                if (e.target.empty() || e.patchRel.empty() || e.baseSha256.size() != 64 || e.resultSha256.size() != 64) {
                    err = "A patch entry in manifest.json needs \"target\", \"patch\", \"base_sha256\" and \"result_sha256\".";
                    return false;
                }
                if (!safeRel(e.target) || ((e.format != "zip-of" && e.format != "builtin") && !safeRel(e.patchRel))) { err = "A patch entry in manifest.json has an unsafe file path."; return false; }
                m.patches.push_back(std::move(e));
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("manifest.json could not be read: ") + e.what();
        return false;
    }
}

std::string readText(const fs::path& p, bool& ok) {
    std::ifstream f(p, std::ios::binary);
    ok = bool(f);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

fs::path manifestIn(const fs::path& folder) {
    std::error_code ec;
    if (fs::is_regular_file(folder / "manifest.json", ec)) return folder / "manifest.json";
    fs::path found;
    int n = 0;
    for (auto& e : fs::directory_iterator(folder, ec)) {
        if (!e.is_directory(ec) || e.is_symlink(ec)) continue;
        if (fs::is_regular_file(e.path() / "manifest.json", ec)) { found = e.path() / "manifest.json"; ++n; }
    }
    return n == 1 ? found : fs::path();
}

struct ZipFile {
    mz_zip_archive z{};
    FILE* f = nullptr;
    bool open = false;
    bool init(const fs::path& p) {
        f = openFile(p, "rb");
        if (!f) return false;
        open = mz_zip_reader_init_cfile(&z, f, 0, 0) != 0;
        return open;
    }
    ~ZipFile() {
        if (open) mz_zip_reader_end(&z);
        if (f) std::fclose(f);
    }
};

std::string zipName(mz_zip_archive& z, mz_uint i) {
    char buf[1024];
    const mz_uint n = mz_zip_reader_get_filename(&z, i, buf, sizeof buf);
    std::string s(buf, n ? n - 1 : 0);
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// Index of manifest.json in the zip and the prefix in front of it ("" or "<folder>/"); -1 when there is none.
int zipManifest(mz_zip_archive& z, std::string& prefix) {
    int best = -1;
    size_t bestDepth = 99;
    const mz_uint n = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < n; ++i) {
        const std::string name = zipName(z, i);
        if (name == "manifest.json") { prefix.clear(); return int(i); }
        const size_t sl = name.find('/');
        if (sl != std::string::npos && name.substr(sl + 1) == "manifest.json" && bestDepth > 1) {
            best = int(i);
            bestDepth = 1;
            prefix = name.substr(0, sl + 1);
        }
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------- scanning and order

bool ModStore::readManifest(const fs::path& manifest, ModInfo& out, std::string& err) const {
    bool ok = false;
    const std::string text = readText(manifest, ok);
    if (!ok) { err = "manifest.json is missing."; return false; }
    return parseManifest(text, out, err);
}

const ModInfo* ModStore::find(const std::string& id) const {
    for (auto& m : mods_)
        if (m.id == id) return &m;
    return nullptr;
}

void ModStore::reload() {
    mods_.clear();
    std::error_code ec;
    if (!fs::is_directory(root_, ec)) return;

    std::vector<ModInfo> found;
    std::set<std::string> seen;
    for (auto& e : fs::directory_iterator(root_, ec)) {
        if (!e.is_directory(ec)) continue;
        const std::string folder = pathToUtf8(e.path().filename());
        if (folder.empty() || folder[0] == '.' || folder[0] == '_') continue;
        ModInfo m;
        m.folder = folder;
        m.dir = e.path();
        std::string err;
        if (!readManifest(e.path() / "manifest.json", m, err)) {
            m.id = folder;
            m.problem = err;
        } else if (!seen.insert(m.id).second) {
            m.problem = "Another mod folder uses the same id (" + m.id + ").";
        }
        found.push_back(std::move(m));
    }

    // order.json: listed ids first (in that order), then by priority and id. Mods that cannot be used go last.
    std::vector<std::string> listed;
    std::map<std::string, bool> flags;
    {
        bool ok = false;
        const std::string text = readText(root_ / "order.json", ok);
        if (ok) {
            try {
                const json j = json::parse(text);
                if (j.contains("order") && j["order"].is_array())
                    for (auto& v : j["order"])
                        if (v.is_string()) listed.push_back(v.get<std::string>());
                if (j.contains("enabled") && j["enabled"].is_object())
                    for (auto& [k, v] : j["enabled"].items())
                        if (v.is_boolean()) flags[k] = v.get<bool>();
            } catch (...) {
                // a damaged order.json is treated as absent; the next save rewrites it
            }
        }
    }
    auto take = [&](const std::string& id) {
        for (size_t i = 0; i < found.size(); ++i)
            if (found[i].problem.empty() && found[i].id == id) {
                mods_.push_back(std::move(found[i]));
                found.erase(found.begin() + long(i));
                return;
            }
    };
    for (auto& id : listed) take(id);
    std::sort(found.begin(), found.end(), [](const ModInfo& a, const ModInfo& b) {
        if (a.problem.empty() != b.problem.empty()) return a.problem.empty();
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.id < b.id;
    });
    for (auto& m : found) mods_.push_back(std::move(m));
    for (auto& m : mods_) {
        const auto it = flags.find(m.id);
        m.enabled = m.problem.empty() && (it != flags.end() ? it->second : m.enabledByManifest);
    }
}

bool ModStore::setEnabled(const std::string& id, bool on) {
    for (auto& m : mods_)
        if (m.id == id && m.problem.empty()) { m.enabled = on; return true; }
    return false;
}

bool ModStore::move(const std::string& id, int delta) {
    size_t i = 0;
    while (i < mods_.size() && mods_[i].id != id) ++i;
    if (i == mods_.size() || !mods_[i].problem.empty()) return false;
    const long j = long(i) + delta;
    if (j < 0 || j >= long(mods_.size()) || !mods_[size_t(j)].problem.empty()) return false;
    std::swap(mods_[i], mods_[size_t(j)]);
    return true;
}

bool ModStore::save(std::string& err) const {
    std::error_code ec;
    fs::create_directories(root_, ec);
    json j = json::object();
    {
        bool ok = false;
        const std::string text = readText(root_ / "order.json", ok);
        if (ok) {
            try {
                const json old = json::parse(text);
                if (old.is_object()) j = old;  // keep keys we do not manage
            } catch (...) {
            }
        }
    }
    json order = json::array(), enabled = json::object();
    for (auto& m : mods_) {
        if (!m.problem.empty()) continue;
        order.push_back(m.id);
        enabled[m.id] = m.enabled;
    }
    j["api"] = kApi;
    j["order"] = order;
    j["enabled"] = enabled;
    const fs::path path = root_ / "order.json";
    const fs::path tmp = root_ / "order.json.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(2);
        if (!f) { err = "Could not save the mod list."; return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) { err = "Could not save the mod list: " + ec.message(); return false; }
    return true;
}

// ---------------------------------------------------------------- does it fit

ModStatus ModStore::status(const ModInfo& m, const std::string& xbe, const std::string& expectedGame) const {
    ModStatus s;
    if (!m.problem.empty()) { s.fit = ModFit::BadManifest; s.text = m.problem; return s; }
    if (m.api > kMaxManifestApi) { s.fit = ModFit::NewerApi; s.text = "Made for a newer mod format. Update the launcher."; return s; }
    if (!m.game.empty() && !expectedGame.empty() && lower(m.game) != lower(expectedGame)) {
        s.fit = ModFit::WrongGame;
        s.text = "Made for a different game (" + m.game + ").";
        return s;
    }
    if (!m.gameHashes.empty() && !xbe.empty()) {
        bool match = false;
        for (std::string h : m.gameHashes) {
            h = lower(h);
            if (h.rfind("sha256:", 0) == 0) h = h.substr(7);
            if (h == lower(xbe)) match = true;
        }
        if (!match) {
            s.fit = ModFit::WrongVersion;
            s.text = "Made for a different version of the game. Your copy does not match, so the game skips it.";
            return s;
        }
    }
    size_t myPos = 0;
    for (size_t i = 0; i < mods_.size(); ++i)
        if (mods_[i].id == m.id) myPos = i;
    for (auto& dep : m.dependencies) {
        size_t pos = 0;
        const ModInfo* d = nullptr;
        for (size_t i = 0; i < mods_.size(); ++i)
            if (mods_[i].id == dep) { d = &mods_[i]; pos = i; }
        if (!d || !d->problem.empty()) { s.fit = ModFit::MissingDependency; s.text = "Needs the mod \"" + dep + "\", which is not installed."; return s; }
        if (m.enabled && !d->enabled) { s.fit = ModFit::DependencyOff; s.text = "Needs \"" + d->title() + "\" turned on."; return s; }
        if (m.enabled && pos > myPos) { s.fit = ModFit::DependencyOff; s.text = "Must come after \"" + d->title() + "\" in the list. Move it down."; return s; }
    }
    if (m.enabled) {
        for (auto& o : mods_) {
            if (o.id == m.id || !o.enabled || !o.problem.empty()) continue;
            const bool a = std::find(m.conflicts.begin(), m.conflicts.end(), o.id) != m.conflicts.end();
            const bool b = std::find(o.conflicts.begin(), o.conflicts.end(), m.id) != o.conflicts.end();
            if (a || b) {
                s.fit = ModFit::Conflict;
                s.text = "Does not work together with \"" + o.title() + "\". Only the one lower in the list is used.";
                return s;
            }
        }
    }
    s.text = "Works with your game.";
    return s;
}

// ---------------------------------------------------------------- install

bool ModStore::looksLikeMod(const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) return !manifestIn(p).empty();
    if (!fs::is_regular_file(p, ec)) return false;
    std::string ext = lower(pathToUtf8(p.extension()));
    if (ext != ".zip") return false;
    ZipFile zf;
    if (!zf.init(p)) return false;
    std::string prefix;
    return zipManifest(zf.z, prefix) >= 0;
}

ModInstallResult ModStore::finishInstall(const fs::path& staged, const ModInfo& info, bool replace) {
    ModInstallResult r;
    r.id = info.id;
    r.name = info.title();
    std::error_code ec;
    reload();
    if (const ModInfo* old = find(info.id)) {
        if (!replace) { r.kind = ModInstallResult::AlreadyInstalled; return r; }
        std::string err;
        const std::string oldId = old->id;  // reload() inside remove() invalidates `old`
        if (!remove(oldId, err)) { r.error = err; return r; }
    }
    // folder name = id, with a number added if that name is taken by something else
    std::string name = info.id;
    for (int n = 2; fs::exists(root_ / name, ec); ++n) name = info.id + "-" + std::to_string(n);
    fs::rename(staged, root_ / name, ec);
    if (ec) { r.error = "Could not put the mod in place: " + ec.message(); return r; }
    reload();
    r.kind = ModInstallResult::Ok;
    return r;
}

ModInstallResult ModStore::installFolder(const fs::path& folder, bool replace) {
    ModInstallResult r;
    std::error_code ec;
    const fs::path manifest = manifestIn(folder);
    if (manifest.empty()) { r.error = "That folder has no manifest.json, so it is not a mod."; return r; }
    ModInfo info;
    std::string err;
    if (!readManifest(manifest, info, err)) { r.error = err; return r; }
    r.id = info.id;
    r.name = info.title();
    reload();
    if (find(info.id) && !replace) { r.kind = ModInstallResult::AlreadyInstalled; r.sourceRoot = folder; return r; }

    const fs::path src = manifest.parent_path();
    // check size and file count first
    unsigned long long total = 0;
    unsigned count = 0;
    for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_symlink(ec) || !it->is_regular_file(ec)) continue;
        total += it->file_size(ec);
        if (++count > kModMaxFiles || total > kModMaxBytes) { r.error = "That mod is too big to install."; return r; }
    }
    fs::create_directories(root_, ec);
    const fs::path staged = root_ / ("_installing_" + stamp());
    fs::create_directories(staged, ec);
    for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_symlink(ec)) continue;
        const fs::path rel = fs::relative(it->path(), src, ec);
        if (it->is_directory(ec)) { fs::create_directories(staged / rel, ec); continue; }
        if (!it->is_regular_file(ec)) continue;
        fs::create_directories((staged / rel).parent_path(), ec);
        fs::copy_file(it->path(), staged / rel, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            r.error = "Copying the mod failed: " + ec.message();
            fs::remove_all(staged, ec);
            return r;
        }
    }
    r = finishInstall(staged, info, replace);
    if (r.kind != ModInstallResult::Ok) fs::remove_all(staged, ec);
    return r;
}

ModInstallResult ModStore::installZip(const fs::path& zipPath, bool replace) {
    ModInstallResult r;
    ZipFile zf;
    if (!zf.init(zipPath)) { r.error = "That file is not a readable .zip."; return r; }
    std::string prefix;
    const int mi = zipManifest(zf.z, prefix);
    if (mi < 0) { r.error = "That zip has no manifest.json, so it is not a mod."; return r; }

    ModInfo info;
    {
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&zf.z, mz_uint(mi), &sz, 0);
        if (!p) { r.error = "The zip's manifest.json could not be read."; return r; }
        const std::string text(static_cast<char*>(p), sz);
        mz_free(p);
        std::string err;
        if (!parseManifest(text, info, err)) { r.error = err; return r; }
    }
    r.id = info.id;
    r.name = info.title();
    reload();
    if (find(info.id) && !replace) { r.kind = ModInstallResult::AlreadyInstalled; r.sourceRoot = zipPath; return r; }

    // check every entry before writing anything
    const mz_uint n = mz_zip_reader_get_num_files(&zf.z);
    unsigned long long total = 0;
    unsigned count = 0;
    for (mz_uint i = 0; i < n; ++i) {
        const std::string name = zipName(zf.z, i);
        if (name.compare(0, prefix.size(), prefix) != 0 || name.size() == prefix.size()) continue;
        if (mz_zip_reader_is_file_a_directory(&zf.z, i)) continue;
        const std::string rel = name.substr(prefix.size());
        if (!safeRel(rel)) { r.error = "That zip contains an unsafe file path (\"" + name + "\"), so it was not installed."; return r; }
        if (mz_zip_reader_is_file_encrypted(&zf.z, i)) { r.error = "That zip is password protected."; return r; }
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zf.z, i, &st)) { r.error = "That zip is damaged."; return r; }
        total += st.m_uncomp_size;
        if (++count > kModMaxFiles || total > kModMaxBytes) { r.error = "That mod is too big to install."; return r; }
    }

    std::error_code ec;
    fs::create_directories(root_, ec);
    const fs::path staged = root_ / ("_installing_" + stamp());
    fs::create_directories(staged, ec);
    auto fail = [&](const std::string& msg) {
        fs::remove_all(staged, ec);
        r.error = msg;
        return r;
    };
    for (mz_uint i = 0; i < n; ++i) {
        const std::string name = zipName(zf.z, i);
        if (name.compare(0, prefix.size(), prefix) != 0 || name.size() == prefix.size()) continue;
        if (mz_zip_reader_is_file_a_directory(&zf.z, i)) continue;
        const fs::path dest = staged / pathFromUtf8(name.substr(prefix.size()));
        fs::create_directories(dest.parent_path(), ec);
        FILE* out = openFile(dest, "wb");
        if (!out) return fail("Could not write the mod's files (disk full or no permission).");
        const mz_bool ok = mz_zip_reader_extract_to_cfile(&zf.z, i, out, 0);
        const bool closed = std::fclose(out) == 0;
        if (!ok || !closed) return fail("The zip is damaged or could not be unpacked (\"" + name + "\").");
    }
    r = finishInstall(staged, info, replace);
    if (r.kind != ModInstallResult::Ok) fs::remove_all(staged, ec);
    return r;
}

bool ModStore::remove(const std::string& id, std::string& err) {
    const ModInfo* m = find(id);
    if (!m) { err = "That mod is not installed."; return false; }
    std::error_code ec;
    const fs::path bin = root_ / "_removed";
    fs::create_directories(bin, ec);
    fs::path dest = bin / (m->folder + "-" + stamp());
    for (int n = 2; fs::exists(dest, ec); ++n) dest = bin / (m->folder + "-" + stamp() + "-" + std::to_string(n));
    fs::rename(m->dir, dest, ec);
    if (ec) { err = "Could not remove the mod (is the game still running?): " + ec.message(); return false; }
    reload();
    std::string serr;
    save(serr);  // drop it from order.json
    return true;
}

void ModStore::seedBundled(const fs::path& bundledDir) {
    std::error_code ec;
    if (root_.empty() || !fs::is_directory(bundledDir, ec)) return;
    for (auto& d : fs::directory_iterator(bundledDir, ec)) {
        if (!d.is_directory(ec)) continue;
        const fs::path marker = root_ / "_seeded" / (d.path().filename().string() + ".txt");
        if (fs::exists(marker, ec)) {
            // Already seeded by an earlier download. If this download carries a changed recipe (a hotfix), swap it
            // in: the old folder goes to _removed, the on/off choice and the load order stay. Needs a rebuild.
            ModInfo bi;
            std::string berr;
            if (!readManifest(d.path() / "manifest.json", bi, berr) || bi.id.empty()) continue;
            const ModInfo* cur = find(bi.id);
            if (!cur) continue;  // the player removed it: stays removed
            auto slurp = [](const fs::path& p) {
                std::ifstream in(p, std::ios::binary);
                return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            };
            if (slurp(cur->dir / "manifest.json") == slurp(d.path() / "manifest.json")) continue;
            const bool wasOn = cur->enabled;
            const ModInstallResult up = installFolder(d.path(), true);
            if (up.kind == ModInstallResult::Ok) {
                reload();
                setEnabled(up.id, wasOn);
                std::string e;
                save(e);
            }
            continue;
        }
        const ModInstallResult r = installFolder(d.path(), false);
        if (r.kind == ModInstallResult::Failed) continue;
        if (r.kind == ModInstallResult::Ok) {
            reload();
            setEnabled(r.id, false);
            std::string e;
            save(e);
        }
        fs::create_directories(marker.parent_path(), ec);
        std::ofstream(marker) << "installed from the download\n";
    }
    reload();
}
