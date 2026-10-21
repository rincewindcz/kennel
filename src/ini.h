#pragma once

#include <string>
#include <vector>

// INI as read by Puppy: [SECTION] headers, KEY=VALUE lines, '#' and ';' comments.
// Section and key lookups are case-insensitive; values are trimmed.
struct IniEntry {
    std::string key, value;
    int line = 0;
};

struct IniSection {
    std::string name;
    int line = 0;
    std::vector<IniEntry> entries;

    const IniEntry* find(std::string_view key) const;
    std::string get(std::string_view key, std::string_view fallback = {}) const;
    bool has(std::string_view key) const { return find(key) != nullptr; }
};

struct IniFile {
    std::string path;
    std::vector<IniSection> sections;

    const IniSection* find(std::string_view name) const;
    std::string get(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
};

IniFile parseIni(const std::string& text, const std::string& path = {});
bool loadIni(const std::string& path, IniFile& out);
