#include "core/config.h"

#include <nlohmann/json.hpp>

#include <fstream>

bool loadProfile(const std::string& path, Profile& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "Could not open the game profile: " + path;
        return false;
    }
    try {
        const auto j = nlohmann::json::parse(f);
        out.brand = j.value("brand", out.brand);
        const auto& g = j.at("game");
        out.gameId = g.at("id").get<std::string>();
        out.gameTitle = g.at("title").get<std::string>();
        out.exe = g.value("exe", std::string());
        const auto& id = g.at("identify");
        out.xbeSha256 = id.at("sha256").get<std::vector<std::string>>();
        out.titleHint = id.value("title_hint", std::string());
        const auto& l = j.at("launch");
        out.gamePathEnv = l.value("game_path_env", out.gamePathEnv);
        out.buttonPromptsEnv = l.value("button_prompts_env", out.buttonPromptsEnv);
        out.modsDirEnv = l.value("mods_dir_env", out.modsDirEnv);
        out.expectedGameId = l.value("mod_game_id", out.expectedGameId);
        out.modsDir = j.value("files", nlohmann::json::object()).value("mods_dir", out.modsDir);
        // (items() of a temporary would dangle, so keep the objects alive in locals)
        const nlohmann::json env = l.value("env", nlohmann::json::object());
        const nlohmann::json iniEnv = l.value("ini_env", nlohmann::json::object());
        for (auto& [k, v] : env.items()) out.env[k] = v.get<std::string>();
        for (auto& [k, v] : iniEnv.items()) out.iniEnv[k] = v.get<std::string>();
        const nlohmann::json im = l.value("input_map", nlohmann::json::object());
        out.inputMapEnv = im.value("env", out.inputMapEnv);
        out.inputMapFile = im.value("file", out.inputMapFile);
        out.inputMapSupported = im.value("supported", false);
        out.updateRepo = j.value("update", nlohmann::json::object()).value("repo", out.updateRepo);
        out.supportUrl = j.value("support", nlohmann::json::object()).value("url", out.supportUrl);
        out.returnToLauncherCode = j.value("exit_codes", nlohmann::json::object()).value("return_to_launcher", out.returnToLauncherCode);
        const nlohmann::json compat = j.value("compat", nlohmann::json::object());
        out.vsyncVrrFixBuild = compat.value("vsync_vrr_fix_build", 0);
        return true;
    } catch (const std::exception& e) {
        err = std::string("The game profile is not valid: ") + e.what();
        return false;
    }
}
