#pragma once

#include "app.h"
#include "widgets.h"

// Quick settings over Home (START): brightness, volume, output and shortcuts.
class QuickPanel : public Screen {
public:
    QuickPanel();
    void input(const KeyEvent& ev) override;
    void draw() override;
    bool overlay() const override { return true; }
    std::vector<Hint> hints() const override;

private:
    Menu menu_;
};
