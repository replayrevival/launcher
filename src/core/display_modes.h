#pragma once
#include <string>
#include <vector>

// Monitors and their resolutions, in physical pixels (never DPI-scaled ones). Pure data:
// the SDL enumeration lives in platform/Displays.cpp, tests feed lists from a JSON file.

struct DisplayMode {
    int w = 0, h = 0;
    float hz = 0;
};

struct DisplayInfo {
    std::string name;
    DisplayMode desktop;             // the monitor's current (native) mode
    float scale = 1.f;               // OS content scale (1.5 = 150 %); informational, sizes are already physical
    std::vector<DisplayMode> modes;  // every mode the monitor reports
};

struct DisplayList {
    std::vector<DisplayInfo> displays;
    int windowDisplay = 0;  // index of the display the launcher window is on
};

struct ResOption {
    int w = 0, h = 0;
    float hz = 0;  // highest refresh rate reported for this size (0 = unknown)
    bool native = false;
    std::string label;
};

// "21:9", "16:9", ... or "" when the shape is not a common one.
std::string aspectName(int w, int h);

// The native resolution first, then every reported mode, one entry per size, widest first.
//   window:     also the usual window sizes that fit the desktop, and "Original 640 x 480 (window)".
//   borderless: also the usual window sizes that fit the desktop.
//   fullscreen: ONLY what the display reports; the refresh rate is added to the label.
std::vector<ResOption> buildResolutionOptions(const DisplayInfo& d, const std::string& mode);

// True when w x h is one of `opts`.
bool inOptions(const std::vector<ResOption>& opts, int w, int h);

// Test input: {"window_display":0,"displays":[{"name":"..","scale":1.5,"desktop":[w,h,hz],"modes":[[w,h,hz],..]}]}
bool loadFakeDisplays(const std::string& jsonPath, DisplayList& out, std::string& err);

// Multi-line text for the log.
std::string describeDisplays(const DisplayList& l);
