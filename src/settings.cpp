#include "settings.h"

#include <algorithm>
#include <cstdlib>

#include "ini.h"
#include "util.h"

Settings settings;

const AccentPreset kAccents[] = {
    {"Blue", 0x4f8cff}, {"Teal", 0x2ec4b6}, {"Amber", 0xffb020},
    {"Rose", 0xff5c8a}, {"Violet", 0x9b7bff}, {"Green", 0x5bd16e}, {"Crimson", 0xdc2847},
};
const int kAccentCount = sizeof kAccents / sizeof kAccents[0];

template <typename E>
static E pick(const std::string& v, std::initializer_list<const char*> names, E fallback) {
    int i = 0;
    for (const char* n : names) {
        if (iequals(v, n)) return (E)i;
        ++i;
    }
    return fallback;
}

void Settings::load(const std::string& file) {
    path = file;
    IniFile ini;
    if (!loadIni(file, ini)) return;
    const IniSection* s = ini.find("kennel");
    if (!s) return;
    auto flag = [&](const char* key, bool& out) {
        if (const IniEntry* e = s->find(key)) out = e->value == "1" || iequals(e->value, "true");
    };
    flag("swap_confirm", swapConfirm);
    time = pick(s->get("time"), {"auto", "clock", "uptime", "off"}, time);
    background = pick(s->get("background"), {"gradient", "art", "wallpaper"}, background);
    wallpaper = s->get("wallpaper");
    animations = pick(s->get("animations"), {"normal", "fast", "off"}, animations);
    sort = pick(s->get("sort"), {"name", "last_played", "play_time"}, sort);
    accent = std::clamp(atoi(s->get("accent", "0").c_str()), 0, kAccentCount);
    parseColor(s->get("accent_custom"), customAccent);
    lowBattery = std::clamp(atoi(s->get("low_battery", std::to_string(lowBattery)).c_str()), 0, 50);
    flag("report_errors", reportErrors);
    flag("show_continue", showContinue);
    flag("show_favorites", showFavorites);
    flag("expert", expert);
    flag("perf_overlay", perfOverlay);
    flag("bar_cpu", barCpu);
    flag("bar_memory", barMemory);
    flag("bar_volume", barVolume);
    flag("bar_battery", barBattery);
    flag("bar_card", barCard);
    flag("bar_usb", barUsb);
    if (const IniEntry* e = s->find("hidden")) {
        hidden = split(e->value, ';');
        hiddenInitialized = true;
    }
    if (const IniEntry* e = s->find("order")) order = split(e->value, ';');
}

bool Settings::save() const {
    auto join = [](const std::vector<std::string>& v) {
        std::string out;
        for (const auto& s : v) out += (out.empty() ? "" : ";") + s;
        return out;
    };
    const char* timeNames[] = {"auto", "clock", "uptime", "off"};
    const char* bgNames[] = {"gradient", "art", "wallpaper"};
    const char* animNames[] = {"normal", "fast", "off"};
    const char* sortNames[] = {"name", "last_played", "play_time"};
    std::string out = "[kennel]\n";
    out += strprintf("swap_confirm=%d\n", swapConfirm);
    out += strprintf("time=%s\n", timeNames[(int)time]);
    out += strprintf("background=%s\n", bgNames[(int)background]);
    out += "wallpaper=" + wallpaper + "\n";
    out += strprintf("accent=%d\n", accent);
    out += strprintf("accent_custom=#%06x\n", customAccent & 0xffffff);
    out += strprintf("animations=%s\n", animNames[(int)animations]);
    out += strprintf("sort=%s\n", sortNames[(int)sort]);
    out += strprintf("low_battery=%d\n", lowBattery);
    out += strprintf("report_errors=%d\n", reportErrors);
    out += strprintf("show_continue=%d\n", showContinue);
    out += strprintf("show_favorites=%d\n", showFavorites);
    out += strprintf("expert=%d\n", expert);
    out += strprintf("perf_overlay=%d\n", perfOverlay);
    out += strprintf("bar_cpu=%d\nbar_memory=%d\nbar_volume=%d\n", barCpu, barMemory, barVolume);
    out += strprintf("bar_battery=%d\nbar_card=%d\nbar_usb=%d\n", barBattery, barCard, barUsb);
    out += "hidden=" + join(hidden) + "\n";
    out += "order=" + join(order) + "\n";
    return writeFileAtomic(path, out);
}

float Settings::animScale() const {
    switch (animations) {
        case Animations::Fast: return 0.5f;
        case Animations::Off: return 0.f;
        default: return 1.f;
    }
}

uint32_t Settings::accentColor() const { return accent < kAccentCount ? kAccents[accent].rgb : customAccent; }
