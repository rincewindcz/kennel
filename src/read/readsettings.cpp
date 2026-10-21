#include "readsettings.h"

#include <algorithm>
#include <cstdlib>

#include "ini.h"
#include "util.h"

const char* const kReadFamilies[] = {"Literata", "Inter"};
const int kReadFamilyCount = 2;

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
    theme = pick(s->get("theme"), {"dark", "sepia", "light"}, theme);
    if (const IniEntry* e = s->find("status_bar")) statusBar = e->value != "0";
}

bool ReadSettings::save() const {
    const char* m[] = {"narrow", "normal", "wide"};
    const char* a[] = {"left", "justify"};
    const char* f[] = {"plain", "styled"};
    const char* p[] = {"indent", "spacing"};
    const char* t[] = {"dark", "sepia", "light"};
    std::string out = "[read]\n";
    out += "family=" + family + "\n";
    out += strprintf("size=%d\nspacing=%d\n", size, spacing);
    out += strprintf("margins=%s\nalign=%s\nformat=%s\n", m[(int)margins], a[(int)align], f[(int)format]);
    out += strprintf("paragraphs=%s\ntheme=%s\nstatus_bar=%d\n", p[(int)paragraphs], t[(int)theme], statusBar);
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
        default: return rgb(0x111318);
    }
}

Color ReadSettings::text() const {
    switch (theme) {
        case Theme::Sepia: return rgb(0x4e3b2a);
        case Theme::Light: return rgb(0x1d1d1f);
        default: return rgb(0xd8d5ce);
    }
}

Color ReadSettings::dim() const {
    switch (theme) {
        case Theme::Sepia: return rgb(0x9a8670);
        case Theme::Light: return rgb(0x8a8a8f);
        default: return rgb(0x6c707a);
    }
}
