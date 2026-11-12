#include "core/display_modes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include "core/identify.h"

std::string aspectName(int w, int h) {
    if (w <= 0 || h <= 0) return "";
    const double r = double(w) / h;
    struct { double r, tol; const char* n; } t[] = {
        {16.0 / 9, 0.03, "16:9"}, {16.0 / 10, 0.03, "16:10"}, {4.0 / 3, 0.03, "4:3"}, {5.0 / 4, 0.03, "5:4"},
        {3.0 / 2, 0.03, "3:2"},   {2.37, 0.09, "21:9"},       {32.0 / 9, 0.08, "32:9"}, {32.0 / 10, 0.05, "32:10"}};
    for (auto& e : t)
        if (std::fabs(r - e.r) <= e.tol) return e.n;
    return "";
}

namespace {

std::string sizeText(int w, int h) { return std::to_string(w) + " \xC3\x97 " + std::to_string(h); }  // "×"

std::string labelFor(const ResOption& o, bool showHz) {
    std::string s = sizeText(o.w, o.h);
    const std::string a = aspectName(o.w, o.h);
    if (!a.empty()) s += " \xC2\xB7 " + a;
    if (o.native) s += " \xC2\xB7 native";
    if (showHz && o.hz > 1) s += " \xC2\xB7 " + std::to_string(int(std::lround(o.hz))) + " Hz";
    return s;
}

}  // namespace

bool inOptions(const std::vector<ResOption>& opts, int w, int h) {
    return std::any_of(opts.begin(), opts.end(), [&](const ResOption& o) { return o.w == w && o.h == h; });
}

std::vector<ResOption> buildResolutionOptions(const DisplayInfo& d, const std::string& mode) {
    const bool fullscreen = mode == "fullscreen", windowed = mode == "window";
    const bool showHz = fullscreen;
    std::vector<ResOption> rest;
    auto add = [&](int w, int h, float hz) {
        if (w < 640 || h < 480) return;
        for (auto& o : rest)
            if (o.w == w && o.h == h) { o.hz = std::max(o.hz, hz); return; }
        ResOption o;
        o.w = w; o.h = h; o.hz = hz;
        rest.push_back(o);
    };
    for (auto& m : d.modes) add(m.w, m.h, m.hz);
    // Sizes people use for a window; only those that fit this monitor.
    if (!fullscreen)
        for (auto s : {std::pair{1280, 720}, std::pair{1600, 900}, std::pair{1920, 1080}, std::pair{2560, 1440}, std::pair{3840, 2160}})
            if (s.first <= d.desktop.w && s.second <= d.desktop.h) add(s.first, s.second, 0);
    if (windowed) add(640, 480, 0);  // the game's own window size

    std::vector<ResOption> out;
    ResOption nat;
    nat.w = d.desktop.w; nat.h = d.desktop.h; nat.hz = d.desktop.hz; nat.native = true;
    out.push_back(nat);
    std::sort(rest.begin(), rest.end(), [](const ResOption& a, const ResOption& b) {
        return a.w != b.w ? a.w > b.w : a.h > b.h;
    });
    for (auto& o : rest)
        if (!(o.w == nat.w && o.h == nat.h)) out.push_back(o);
    for (auto& o : out) o.label = labelFor(o, showHz);
    if (windowed)
        for (auto& o : out)
            if (o.w == 640 && o.h == 480) o.label = "Original 640 \xC3\x97 480 (window)";
    return out;
}

bool loadFakeDisplays(const std::string& path, DisplayList& out, std::string& err) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    if (!f) { err = "Could not open the test display list: " + path; return false; }
    try {
        const nlohmann::json j = nlohmann::json::parse(f);
        out = {};
        out.windowDisplay = j.value("window_display", 0);
        auto mode = [](const nlohmann::json& a) {
            DisplayMode m;
            m.w = a.at(0).get<int>();
            m.h = a.at(1).get<int>();
            m.hz = a.size() > 2 ? a.at(2).get<float>() : 0.f;
            return m;
        };
        for (auto& d : j.at("displays")) {
            DisplayInfo di;
            di.name = d.value("name", "Display");
            di.scale = d.value("scale", 1.0f);
            di.desktop = mode(d.at("desktop"));
            for (auto& m : d.value("modes", nlohmann::json::array())) di.modes.push_back(mode(m));
            out.displays.push_back(std::move(di));
        }
        return !out.displays.empty();
    } catch (const std::exception& e) {
        err = std::string("The test display list is not valid: ") + e.what();
        return false;
    }
}

std::string describeDisplays(const DisplayList& l) {
    std::string s;
    char buf[256];
    for (size_t i = 0; i < l.displays.size(); ++i) {
        const DisplayInfo& d = l.displays[i];
        std::snprintf(buf, sizeof buf, "display %zu%s: \"%s\" native %dx%d @ %.0f Hz, OS scale %.0f%%, %zu modes\n", i,
                      int(i) == l.windowDisplay ? " (launcher window)" : "", d.name.c_str(), d.desktop.w, d.desktop.h, d.desktop.hz,
                      d.scale * 100.f, d.modes.size());
        s += buf;
        std::string line = "  offered:";
        for (auto& o : buildResolutionOptions(d, "fullscreen")) line += "\n    " + o.label;
        s += line + "\n";
    }
    return s;
}
