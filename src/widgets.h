#pragma once

#include <functional>
#include <string>
#include <vector>

#include "anim.h"
#include "app.h"
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
    bool danger = false;

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

// Full-screen list with a title, used by settings, expert mode and their sub-pages.
class MenuScreen : public Screen {
public:
    MenuScreen(std::string title, std::string icon, std::function<std::vector<MenuItem>()> build);
    void input(const KeyEvent& ev) override;
    void draw() override;
    std::vector<Hint> hints() const override;
    std::string title() const override { return title_; }
    void resumed() override;
    void rebuild();

    std::function<void(Key)> extraKey;
    std::vector<Hint> extraHints;
    std::string subtitle;

protected:
    std::string title_, icon_;
    std::function<std::vector<MenuItem>()> build_;
    Menu menu_;
};

class DialogScreen : public Screen {
public:
    DialogScreen(std::string icon, std::string title, std::string message, std::vector<DialogButton> buttons, int selected);
    void input(const KeyEvent& ev) override;
    void draw() override;
    bool overlay() const override { return true; }
    std::vector<Hint> hints() const override;

private:
    void choose(int i);
    std::string icon_, title_, message_;
    std::vector<DialogButton> buttons_;
    int selected_;
};

void drawBackground(float alpha = 1.f);
void drawPageHeader(const std::string& icon, const std::string& title, const std::string& subtitle, float alpha);
void drawScrollbar(const SDL_FRect& track, float offset, float content, float view, float alpha);
Color accent(float a = 1.f);
