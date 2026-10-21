#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// Logical buttons. Confirm and Back follow the confirm-button setting (south or east face button).
enum class Key { None, Up, Down, Left, Right, Confirm, Back, X, Y, L1, R1, L2, R2, L3, R3, Start, Select };

struct KeyEvent {
    Key key = Key::None;
    bool repeat = false;
};

class Input {
public:
    void handle(const SDL_Event& ev, std::vector<KeyEvent>& out);
    void tick(std::vector<KeyEvent>& out);
    int timeoutMs() const;
    void releaseAll();
    std::string label(Key k) const;
    std::string padName() const;

private:
    Key fromButton(int button) const;
    Key fromKeycode(SDL_Keycode k) const;
    void press(Key k, std::vector<KeyEvent>& out);
    void release(Key k);
    void stick(std::vector<KeyEvent>& out);

    SDL_Gamepad* pad_ = nullptr;
    Key held_ = Key::None;
    double heldSince_ = 0, nextRepeat_ = 0;
    int stickX_ = 0, stickY_ = 0;
    Key stickDir_ = Key::None;
    bool l2_ = false, r2_ = false;
};

extern Input input;
