#include "ini.h"

#include "util.h"

const IniEntry* IniSection::find(std::string_view key) const {
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (iequals(it->key, key)) return &*it;
    return nullptr;
}

std::string IniSection::get(std::string_view key, std::string_view fallback) const {
    const IniEntry* e = find(key);
    return e ? e->value : std::string(fallback);
}

const IniSection* IniFile::find(std::string_view name) const {
    for (const auto& s : sections)
        if (iequals(s.name, name)) return &s;
    return nullptr;
}

std::string IniFile::get(std::string_view section, std::string_view key, std::string_view fallback) const {
    const IniSection* s = find(section);
    return s ? s->get(key, fallback) : std::string(fallback);
}

IniFile parseIni(const std::string& text, const std::string& path) {
    IniFile ini;
    ini.path = path;
    ini.sections.push_back({"", 0, {}});
    size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(std::string_view(text).substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            ini.sections.push_back({trim(line.substr(1, line.size() - 2)), lineNo, {}});
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        ini.sections.back().entries.push_back({trim(line.substr(0, eq)), trim(line.substr(eq + 1)), lineNo});
    }
    return ini;
}

bool loadIni(const std::string& path, IniFile& out) {
    auto text = readFile(path);
    if (!text) return false;
    out = parseIni(*text, path);
    return true;
}
