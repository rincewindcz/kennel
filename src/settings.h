#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Settings {
    enum class TimeMode { Auto, Clock, Uptime, Off };
    enum class Background { Gradient, Art, Wallpaper };
    enum class Animations { Normal, Fast, Off };
    enum class Sort { Name, LastPlayed, PlayTime };

    bool swapConfirm = false;
    TimeMode time = TimeMode::Auto;
    Background background = Background::Art;
    std::string wallpaper;  // image for Background::Wallpaper
    int accent = 0;  // index into kAccents; kAccentCount is the custom colour
    uint32_t customAccent = 0x4f8cff;
    Animations animations = Animations::Normal;
    Sort sort = Sort::Name;
    int lowBattery = 15;
    int screenOffAfter = 0;  // seconds without input, 0: never
    bool reportErrors = true;
    bool showContinue = true;
    bool showFavorites = true;
    bool expert = false;
    bool perfOverlay = false;
    bool barCpu = true, barMemory = true, barVolume = true, barBattery = true, barCard = true, barUsb = true;
    bool hudNextLaunch = false;
    std::vector<std::string> hidden;      // category names hidden on Home
    std::vector<std::string> order;       // category names in user order; others follow
    bool hiddenInitialized = false;

    void load(const std::string& path);
    bool save() const;
    float animScale() const;
    uint32_t accentColor() const;

    std::string path;
};

struct AccentPreset {
    const char* name;
    uint32_t rgb;
};
extern const AccentPreset kAccents[];
extern const int kAccentCount;

extern Settings settings;
