#include "readsettings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "ini.h"
#include "paths.h"
#include "util.h"

std::vector<std::string> readFamilies() {
    std::vector<std::string> out;
    for (const std::string& dir : {paths.data + "/fonts", paths.asset("fonts/read")})
        for (const auto& file : glob(dir + "/*-Regular.ttf")) {
            std::string stem = baseName(file);
            stem.resize(stem.size() - strlen("-Regular.ttf"));
            if (stem.find(' ') != std::string::npos) {
                out.push_back(stem);
                continue;
            }
            std::string name;
            for (size_t i = 0; i < stem.size(); ++i) {
                bool upper = isupper((unsigned char)stem[i]);
                bool prevLower = i > 0 && islower((unsigned char)stem[i - 1]);
                bool acronymEnd = i > 0 && isupper((unsigned char)stem[i - 1]) && i + 1 < stem.size() &&
                                  islower((unsigned char)stem[i + 1]);
                if (upper && (prevLower || acronymEnd)) name += ' ';
                name += stem[i];
            }
            out.push_back(name);
        }
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

template <typename E>
static E pick(const std::string& v, std::initializer_list<const char*> names, E fallback) {
    int i = 0;
    for (const char* n : names) {
        if (iequals(v, n)) return (E)i;
        ++i;
    }
    return fallback;
}

void ReadSettings::load(const std::string& file) {
    path = file;
    IniFile ini;
    if (!loadIni(file, ini)) return;
    const IniSection* s = ini.find("read");
    if (!s) return;
    family = s->get("family", family);
    size = std::clamp(atoi(s->get("size", std::to_string(size)).c_str()), 14, 36);
    spacing = std::clamp(atoi(s->get("spacing", std::to_string(spacing)).c_str()), 10, 20);
    margins = pick(s->get("margins"), {"narrow", "normal", "wide"}, margins);
    align = pick(s->get("align"), {"left", "justify"}, align);
    format = pick(s->get("format"), {"plain", "styled"}, format);
    paragraphs = pick(s->get("paragraphs"), {"indent", "spacing"}, paragraphs);
    theme = pick(s->get("theme"), {"dark", "sepia", "light", "custom"}, theme);
    parseColor(s->get("custom_bg"), customBg);
    parseColor(s->get("custom_fg"), customFg);
    auto flag = [s](const char* key, bool& v) {
        if (const IniEntry* e = s->find(key)) v = e->value != "0";
    };
    flag("status_bar", statusBar);
    flag("status_chapter", showChapter);
    flag("status_pages", showPages);
    flag("status_progress", showProgress);
    flag("status_time", showTime);
    flag("status_battery", showBattery);
}

bool ReadSettings::save() const {
    const char* m[] = {"narrow", "normal", "wide"};
    const char* a[] = {"left", "justify"};
    const char* f[] = {"plain", "styled"};
    const char* p[] = {"indent", "spacing"};
    const char* t[] = {"dark", "sepia", "light", "custom"};
    std::string out = "[read]\n";
    out += "family=" + family + "\n";
    out += strprintf("size=%d\nspacing=%d\n", size, spacing);
    out += strprintf("margins=%s\nalign=%s\nformat=%s\n", m[(int)margins], a[(int)align], f[(int)format]);
    out += strprintf("paragraphs=%s\ntheme=%s\nstatus_bar=%d\n", p[(int)paragraphs], t[(int)theme], statusBar);
    out += strprintf("status_chapter=%d\nstatus_pages=%d\nstatus_progress=%d\nstatus_time=%d\nstatus_battery=%d\n", showChapter,
                     showPages, showProgress, showTime, showBattery);
    out += strprintf("custom_bg=#%06x\ncustom_fg=#%06x\n", customBg & 0xffffff, customFg & 0xffffff);
    return writeFileAtomic(path, out);
}

int ReadSettings::marginPx() const {
    switch (margins) {
        case Margins::Narrow: return 12;
        case Margins::Wide: return 48;
        default: return 26;
    }
}

Color ReadSettings::background() const {
    switch (theme) {
        case Theme::Sepia: return rgb(0xf3ead6);
        case Theme::Light: return rgb(0xfafafa);
        case Theme::Custom: return rgb(customBg);
        default: return rgb(0x111318);
    }
}

Color ReadSettings::text() const {
    switch (theme) {
        case Theme::Sepia: return rgb(0x4e3b2a);
        case Theme::Light: return rgb(0x1d1d1f);
        case Theme::Custom: return rgb(customFg);
        default: return rgb(0xd8d5ce);
    }
}

Color ReadSettings::dim() const {
    switch (theme) {
        case Theme::Sepia: return rgb(0x9a8670);
        case Theme::Light: return rgb(0x8a8a8f);
        case Theme::Custom: return mix(rgb(customFg), rgb(customBg), 0.45f);
        default: return rgb(0x6c707a);
    }
}
