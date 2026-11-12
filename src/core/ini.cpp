#include "core/ini.h"

#include <algorithm>
#include <cctype>

namespace {
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
}  // namespace

void Ini::parse(const std::string& text) {
    lines_.clear();
    eol_ = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        std::string raw = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        Line l;
        l.raw = raw;
        const std::string t = trim(raw);
        if (!t.empty() && t[0] == '[' && t.back() == ']') {
            section = lower(trim(t.substr(1, t.size() - 2)));
            l.header = true;
        } else if (!t.empty() && t[0] != ';' && t[0] != '#') {
            const size_t eq = t.find('=');
            if (eq != std::string::npos) l.key = lower(trim(t.substr(0, eq)));
        }
        l.section = section;
        lines_.push_back(std::move(l));
    }
}

std::string Ini::text() const {
    std::string out;
    for (const auto& l : lines_) out += l.raw + eol_;
    return out;
}

int Ini::findKey(const std::string& section, const std::string& key) const {
    const std::string s = lower(section), k = lower(key);
    for (size_t i = 0; i < lines_.size(); ++i)
        if (!lines_[i].key.empty() && lines_[i].section == s && lines_[i].key == k) return int(i);
    return -1;
}

std::optional<std::string> Ini::get(const std::string& section, const std::string& key) const {
    const int i = findKey(section, key);
    if (i < 0) return std::nullopt;
    const std::string& raw = lines_[i].raw;
    return trim(raw.substr(raw.find('=') + 1));
}

void Ini::set(const std::string& section, const std::string& key, const std::string& value) {
    const std::string s = lower(section);
    Line nl;
    nl.raw = key + "=" + value;
    nl.section = s;
    nl.key = lower(key);

    const int i = findKey(section, key);
    if (i >= 0) { lines_[i].raw = nl.raw; return; }

    // Insert after the last key of the section, else right after its header, else a new section at the end.
    int at = -1;
    for (size_t j = 0; j < lines_.size(); ++j)
        if (lines_[j].section == s && (lines_[j].header || !lines_[j].key.empty())) at = int(j);
    if (at >= 0) { lines_.insert(lines_.begin() + at + 1, nl); return; }
    Line h;
    h.raw = "[" + s + "]";
    h.section = s;
    h.header = true;
    if (!lines_.empty() && !lines_.back().raw.empty()) lines_.push_back(Line{"", lines_.back().section, "", false});
    lines_.push_back(h);
    lines_.push_back(nl);
}

void Ini::unset(const std::string& section, const std::string& key) {
    const int i = findKey(section, key);
    if (i >= 0) lines_.erase(lines_.begin() + i);
}
