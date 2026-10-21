#pragma once

#include <functional>
#include <string>
#include <vector>

#include "anim.h"
#include "gfx.h"
#include "input.h"

struct MenuItem {
    enum class Type { Action, Toggle, Choice, Slider, Link, Info, Header };

    Type type = Type::Action;
    std::string icon, label;
    std::function<void()> activate;
    std::function<bool()> getBool;
    std::function<void(bool)> setBool;
    std::vector<std::string> choices;
    std::function<int()> getIndex;
    std::function<void(int)> setIndex;
    int min = 0, max = 100, step = 5;
    std::function<int()> getInt;
    std::function<void(int)> setInt;
    std::function<std::string()> value;
    std::function<std::string(int)> format;  // slider value text, default "N%"
    std::function<Color()> swatch;          // colour square left of the value
    bool danger = false;
    bool indent = false;  // aligned with the labels of rows that have an icon

    bool selectable() const { return type != Type::Header; }
};

namespace items {
MenuItem header(std::string label);
MenuItem action(std::string icon, std::string label, std::function<void()> f, bool danger = false);
MenuItem link(std::string icon, std::string label, std::function<void()> f);
MenuItem info(std::string icon, std::string label, std::function<std::string()> value);
MenuItem toggle(std::string icon, std::string label, std::function<bool()> get, std::function<void(bool)> set);
MenuItem choice(std::string icon, std::string label, std::vector<std::string> choices, std::function<int()> get,
                std::function<void(int)> set);
MenuItem slider(std::string icon, std::string label, int min, int max, int step, std::function<int()> get,
                std::function<void(int)> set);
// Slider over raw hardware levels 1..maxLevel (e.g. a backlight), shown in percent. Every step
// changes the level, also when the device has only a few levels.
MenuItem level(std::string icon, std::string label, int maxLevel, std::function<int()> get, std::function<void(int)> set);
}  // namespace items

// Vertical list with icons, values, toggles, choices and sliders.
class Menu {
public:
    std::vector<MenuItem> items;
    int selected = 0;
    float rowHeight = 40;
    bool compact = false;

    bool input(const KeyEvent& ev);
    void draw(const SDL_FRect& area, float alpha = 1.f);
    void fixSelection();
    const MenuItem* current() const;

private:
    void move(int dir, bool wrap);
    anim::Tween scroll_, cursor_;
    bool placed_ = false;
};

void drawScrollbar(const SDL_FRect& track, float offset, float content, float view, float alpha);
Color accent(float a = 1.f);
