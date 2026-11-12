#pragma once
#include <optional>
#include <string>
#include <vector>

// Line-preserving ini editor: comments, blank lines and keys we do not manage stay as they were.
class Ini {
public:
    void parse(const std::string& text);
    std::string text() const;

    std::optional<std::string> get(const std::string& section, const std::string& key) const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    void unset(const std::string& section, const std::string& key);  // removes the active line

private:
    struct Line {
        std::string raw;
        std::string section;  // section this line belongs to
        std::string key;      // non-empty for "key=value" lines
        bool header = false;
    };
    int findKey(const std::string& section, const std::string& key) const;
    std::vector<Line> lines_;
    std::string eol_ = "\n";
};
