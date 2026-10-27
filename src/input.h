#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// Logical buttons. Confirm and Back follow the confirm-button setting (south or east face button).
enum class Key { None, Up, Down, Left, Right, Confirm, Back, X, Y, L1, R1, L2, R2, L3, R3, Start, Select };

struct KeyEvent {
    Key key = Key::None;
    bool repeat = false;
    bool stick = false;  // from the analog stick rather than the d-pad or a key
};

class Input {
public:
    void handle(const SDL_Event& ev, std::vector<KeyEvent>& out);
    void tick(std::vector<KeyEvent>& out);
    int timeoutMs() const;
    void releaseAll();
    std::string label(Key k) const;
    std::string padName() const;

    bool repeatAll = false;  // face buttons and triggers repeat too (typing)

private:
    Key fromButton(int button) const;
    Key fromKeycode(SDL_Keycode k) const;
    void press(Key k, std::vector<KeyEvent>& out, bool stick = false);
    void release(Key k);
    void stick(std::vector<KeyEvent>& out);
    bool repeats(Key k) const;

    SDL_Gamepad* pad_ = nullptr;
    Key held_ = Key::None;
    bool heldStick_ = false;
    double heldSince_ = 0, nextRepeat_ = 0;
    int stickX_ = 0, stickY_ = 0;
    Key stickDir_ = Key::None;
    bool l2_ = false, r2_ = false;
};

extern Input input;

// Waits up to timeoutMs for an event, forever below 0. SDL3 can't wait for input on KMSDRM and polls
// every 1 ms there; this polls every intervalMs instead.
bool waitEvent(SDL_Event* ev, int timeoutMs, int intervalMs);
