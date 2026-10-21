#pragma once

#include <string>
#include <vector>

#include "gfx.h"
#include "input.h"

// On-screen keyboard for the d-pad. Up/Down/Left/Right move the cursor, Confirm presses the key,
// Back is backspace, X space, Start enter, L2 shift and R2 the symbol layer. Other buttons are left
// to the caller.
class Keyboard {
public:
    enum class Action { None, Text, Backspace, Enter, Esc, Tab, Left, Right, Up, Down, Home, End, PageUp, PageDown, Menu };
    struct Result {
        bool handled = false;
        Action action = Action::None;
        std::string text;  // Action::Text
        bool ctrl = false;  // Ctrl was latched for this key
    };

    explicit Keyboard(bool terminalKeys = false);
    Result input(const KeyEvent& ev);
    void draw(const SDL_FRect& area, float alpha = 1.f);
    float height() const;
    bool shifted() const { return shift_ || caps_; }

private:
    enum Layer { kLower, kUpper, kSymbols };
    struct Def {
        std::string label, text, icon;
        Action action = Action::Text;
        float width = 1;
        enum Special { None, Shift, Layer, Ctrl } special = None;
    };
    using Row = std::vector<Def>;

    const std::vector<Row>& rows() const;
    int layer() const;
    Result handle(const KeyEvent& ev);
    Result press(const Def& d);
    void moveRow(int dir);
    float centre(int row, int col) const;

    bool terminal_;
    std::vector<Row> layers_[3];  // with the terminal row first when terminalKeys
    bool symbols_ = false;
    bool shift_ = false, caps_ = false, ctrl_ = false;
    int row_ = 0, col_ = 0;
};
