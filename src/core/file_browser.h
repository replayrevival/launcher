#pragma once
// A file browser that works with a pad, keyboard or mouse alike (the launcher draws it as a list of buttons). No window, no SDL:
// drives, quick places, recent folders, folders and the files that match a filter. The Windows file dialog stays available as an
// option for mouse users (the launcher offers a button for it).
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

enum class BrowseKind {
    File,    // pick one file (filtered by extension)
    Folder,  // pick the folder you are in ("USE THIS FOLDER")
};

struct BrowseEntry {
    std::string name;      // shown text ("C:\ (Local disk)" for a drive)
    bool dir = false;
    uint64_t size = 0;     // files only
    std::filesystem::path path;
};

struct BrowsePlace {
    std::string label;
    std::filesystem::path path;
};

class FileBrowser {
public:
    // `exts`: lower-case extensions with the dot (".iso"); empty = every file. `recents` are the folders chosen before, newest first.
    // `start` empty = the newest recent folder that still exists, else Documents, else the drive list.
    void open(BrowseKind kind, const std::string& title, std::vector<std::string> exts, const std::filesystem::path& start,
              std::vector<std::filesystem::path> recents);

    BrowseKind kind() const { return kind_; }
    const std::string& title() const { return title_; }
    const std::vector<std::string>& exts() const { return exts_; }
    const std::filesystem::path& dir() const { return dir_; }   // empty = the drive list ("This PC")
    bool atDriveList() const { return dir_.empty(); }
    const std::vector<BrowseEntry>& entries() const { return entries_; }
    bool truncated() const { return truncated_; }               // more entries than are listed
    const std::vector<BrowsePlace>& places() const { return places_; }    // Desktop, Documents, Downloads
    const std::vector<std::filesystem::path>& recents() const { return recents_; }
    std::string pathText() const;                               // "This PC" or the folder path

    // Opens entry `idx`: a folder / drive is entered (returns false); a file is picked (returns true, `picked` set).
    bool enter(size_t idx, std::filesystem::path& picked);
    bool up();                          // parent folder, or the drive list from a drive root; false when already at the drive list
    void goTo(const std::filesystem::path& p);
    void goDriveList();

    static std::string sizeText(uint64_t bytes);                // "4.2 GB"

private:
    void refresh();
    bool matches(const std::filesystem::path& file) const;

    BrowseKind kind_ = BrowseKind::File;
    std::string title_;
    std::vector<std::string> exts_;
    std::filesystem::path dir_;
    std::vector<BrowseEntry> entries_;
    bool truncated_ = false;
    std::vector<BrowsePlace> places_;
    std::vector<std::filesystem::path> recents_;
};
