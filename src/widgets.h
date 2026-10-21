#pragma once

#include <functional>
#include <string>
#include <vector>

#include "anim.h"
#include "app.h"
#include "gfx.h"
#include "input.h"
#include "menu.h"

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
