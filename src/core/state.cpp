#include "core/state.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace fs = std::filesystem;
using nlohmann::json;

void statPath(const fs::path& p, uint64_t& size, int64_t& mtime) {
    std::error_code ec;
    size = fs::is_regular_file(p, ec) ? fs::file_size(p, ec) : 0;
    const auto t = fs::last_write_time(p, ec);
    mtime = ec ? 0 : int64_t(t.time_since_epoch().count());
}

void LauncherState::clearGame() {
    gamePath.clear(); gameKind.clear(); gameTitle.clear(); gameRegion.clear(); sha256.clear();
    pathSize = 0; pathMtime = 0;
    sourceImage.clear(); sourceSize = 0; sourceMtime = 0;
}

bool LauncherState::load(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    try {
        const json j = json::parse(f);
        gamePath = j.value("game_path", "");
        gameKind = j.value("game_kind", "");
        gameTitle = j.value("game_title", "");
        gameRegion = j.value("game_region", "");
        sha256 = j.value("sha256", "");
        pathSize = j.value("path_size", uint64_t(0));
        pathMtime = j.value("path_mtime", int64_t(0));
        sourceImage = j.value("source_image", "");
        sourceSize = j.value("source_size", uint64_t(0));
        sourceMtime = j.value("source_mtime", int64_t(0));
        displayChoice = j.value("display_choice", -1);
        uiSize = j.value("ui_size", 100);
        vrrDefaultedOff = j.value("vrr_defaulted_off", false);
        updateCheck = j.value("update_check", true);
        lastUpdateCheck = j.value("last_update_check", int64_t(0));
        latestTag = j.value("latest_tag", "");
        latestUrl = j.value("latest_url", "");
        return true;
    } catch (...) {
        return false;  // a damaged state file just means "ask again"
    }
}

bool LauncherState::save(const fs::path& file, std::string& err) const {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    json j = {{"game_path", gamePath}, {"game_kind", gameKind}, {"game_title", gameTitle}, {"game_region", gameRegion},
              {"sha256", sha256}, {"path_size", pathSize}, {"path_mtime", pathMtime},
              {"source_image", sourceImage}, {"source_size", sourceSize}, {"source_mtime", sourceMtime},
              {"display_choice", displayChoice}, {"ui_size", uiSize},
              {"vrr_defaulted_off", vrrDefaultedOff}, {"update_check", updateCheck}, {"last_update_check", lastUpdateCheck},
              {"latest_tag", latestTag}, {"latest_url", latestUrl}};
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump(2);
        if (!f) { err = "Could not save launcher settings: " + tmp.string(); return false; }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        fs::remove(file, ec);
        fs::rename(tmp, file, ec);
        if (ec) { err = "Could not save launcher settings: " + ec.message(); return false; }
    }
    return true;
}
