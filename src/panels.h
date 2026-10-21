#pragma once

#include "app.h"
#include "widgets.h"

// Side panel with a menu over the current screen. Links close the panel before they open their
// page; with closeOnAction, actions do too.
class MenuPanel : public Screen {
public:
    explicit MenuPanel(std::string title, std::vector<MenuItem> items = {}, bool closeOnAction = true);
    void input(const KeyEvent& ev) override;
    void draw() override;
    bool overlay() const override { return true; }
    std::vector<Hint> hints() const override;

protected:
    std::string title_;
    bool closeOnAction_;
    Menu menu_;
};

// Quick settings over Home (START): brightness, volume, output and shortcuts.
class QuickPanel : public MenuPanel {
public:
    QuickPanel();
};
