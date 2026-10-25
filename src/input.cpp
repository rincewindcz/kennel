#include "input.h"

#include <cmath>
#include <cstdlib>

#include "settings.h"
#include "util.h"

Input input;

namespace {
constexpr double kRepeatDelay = 0.35, kRepeatRate = 0.06, kFastRate = 0.03, kFastAfter = 2.0;
constexpr int kStickPress = 16000, kStickRelease = 8000, kTriggerPress = 16000;

// Events are handled before the frame clock advances past a blocking wait, so input reads the clock
// itself.
double clockNow() { return SDL_GetTicksNS() / 1e9; }

}  // namespace

bool Input::repeats(Key k) const {
    if (repeatAll) return k != Key::Start && k != Key::Select && k != Key::None;
    return k == Key::Up || k == Key::Down || k == Key::Left || k == Key::Right || k == Key::L1 || k == Key::R1;
}

Key Input::fromButton(int b) const {
    bool swap = settings.swapConfirm;
    switch (b) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return Key::Up;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return Key::Down;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return Key::Left;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Key::Right;
        case SDL_GAMEPAD_BUTTON_SOUTH: return swap ? Key::Back : Key::Confirm;
        case SDL_GAMEPAD_BUTTON_EAST: return swap ? Key::Confirm : Key::Back;
        case SDL_GAMEPAD_BUTTON_WEST: return Key::X;
        case SDL_GAMEPAD_BUTTON_NORTH: return Key::Y;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return Key::L1;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return Key::R1;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return Key::L3;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return Key::R3;
        case SDL_GAMEPAD_BUTTON_START: return Key::Start;
        case SDL_GAMEPAD_BUTTON_BACK:
        case SDL_GAMEPAD_BUTTON_GUIDE: return Key::Select;
        default: return Key::None;
    }
}

Key Input::fromKeycode(SDL_Keycode k) const {
    switch (k) {
        case SDLK_UP: return Key::Up;
        case SDLK_DOWN: return Key::Down;
        case SDLK_LEFT: return Key::Left;
        case SDLK_RIGHT: return Key::Right;
        case SDLK_RETURN:
        case SDLK_Z: return Key::Confirm;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
        case SDLK_X: return Key::Back;
        case SDLK_A: return Key::X;
        case SDLK_S: return Key::Y;
        case SDLK_Q: return Key::L1;
        case SDLK_W: return Key::R1;
        case SDLK_1: return Key::L2;
        case SDLK_2: return Key::R2;
        case SDLK_SPACE: return Key::Start;
        case SDLK_TAB: return Key::Select;
        default: return Key::None;
    }
}

void Input::press(Key k, std::vector<KeyEvent>& out, bool stick) {
    if (k == Key::None) return;
    out.push_back({k, false, stick});
    if (repeats(k)) {
        held_ = k;
        heldStick_ = stick;
        heldSince_ = clockNow();
        nextRepeat_ = heldSince_ + kRepeatDelay;
    }
}

void Input::release(Key k) {
    if (k == held_) held_ = Key::None;
}

void Input::releaseAll() {
    held_ = Key::None;
    stickDir_ = Key::None;
}

void Input::stick(std::vector<KeyEvent>& out) {
    // Hysteresis: a direction holds until the stick falls back towards the centre, and changes
    // only on a deliberate push, so a stick springing back does not produce a stray move.
    int ax = std::abs(stickX_), ay = std::abs(stickY_);
    Key dir = ax >= ay ? (stickX_ < 0 ? Key::Left : Key::Right) : (stickY_ < 0 ? Key::Up : Key::Down);
    int pushed = std::max(ax, ay);
    Key next = Key::None;
    if (stickDir_ != Key::None) {
        int along = stickDir_ == Key::Left ? -stickX_ : stickDir_ == Key::Right ? stickX_ : stickDir_ == Key::Up ? -stickY_ : stickY_;
        if (dir != stickDir_ && pushed >= kStickPress) next = dir;
        else next = along >= kStickRelease ? stickDir_ : Key::None;
    } else if (pushed >= kStickPress) {
        next = dir;
    }
    if (next == stickDir_) return;
    if (held_ == stickDir_) held_ = Key::None;
    stickDir_ = next;
    press(next, out, true);
}

void Input::handle(const SDL_Event& ev, std::vector<KeyEvent>& out) {
    switch (ev.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pad_) {
                pad_ = SDL_OpenGamepad(ev.gdevice.which);
                if (pad_) logf("gamepad: %s", SDL_GetGamepadName(pad_));
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pad_ && SDL_GetGamepadID(pad_) == ev.gdevice.which) {
                SDL_CloseGamepad(pad_);
                pad_ = nullptr;
                releaseAll();
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: press(fromButton(ev.gbutton.button), out); break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP: release(fromButton(ev.gbutton.button)); break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            switch (ev.gaxis.axis) {
                case SDL_GAMEPAD_AXIS_LEFTX: stickX_ = ev.gaxis.value; stick(out); break;
                case SDL_GAMEPAD_AXIS_LEFTY: stickY_ = ev.gaxis.value; stick(out); break;
                case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
                case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: {
                    bool left = ev.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                    bool& state = left ? l2_ : r2_;
                    bool now = ev.gaxis.value >= kTriggerPress;
                    if (now && !state) press(left ? Key::L2 : Key::R2, out);
                    else if (!now && state) release(left ? Key::L2 : Key::R2);
                    state = now;
                    break;
                }
                default: break;
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!ev.key.repeat) press(fromKeycode(ev.key.key), out);
            break;
        case SDL_EVENT_KEY_UP: release(fromKeycode(ev.key.key)); break;
        default: break;
    }
}

void Input::tick(std::vector<KeyEvent>& out) {
    if (held_ == Key::None) return;
    double now = clockNow();
    if (now < nextRepeat_) return;
    out.push_back({held_, true, heldStick_});
    nextRepeat_ = now + (now - heldSince_ > kFastAfter ? kFastRate : kRepeatRate);
}

int Input::timeoutMs() const {
    if (held_ == Key::None) return -1;
    return std::max(0, (int)std::ceil((nextRepeat_ - clockNow()) * 1000));
}

std::string Input::label(Key k) const {
    auto face = [&](SDL_GamepadButton b, const char* fallback) -> std::string {
        if (!pad_) return fallback;
        switch (SDL_GetGamepadButtonLabel(pad_, b)) {
            case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
            case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
            case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
            case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
            default: return fallback;
        }
    };
    bool swap = settings.swapConfirm;
    switch (k) {
        case Key::Confirm: return swap ? face(SDL_GAMEPAD_BUTTON_EAST, "B") : face(SDL_GAMEPAD_BUTTON_SOUTH, "A");
        case Key::Back: return swap ? face(SDL_GAMEPAD_BUTTON_SOUTH, "A") : face(SDL_GAMEPAD_BUTTON_EAST, "B");
        case Key::X: return face(SDL_GAMEPAD_BUTTON_WEST, "X");
        case Key::Y: return face(SDL_GAMEPAD_BUTTON_NORTH, "Y");
        case Key::L1: return "L1";
        case Key::R1: return "R1";
        case Key::L2: return "L2";
        case Key::R2: return "R2";
        case Key::L3: return "L3";
        case Key::R3: return "R3";
        case Key::Start: return "START";
        case Key::Select: return "SELECT";
        default: return "";
    }
}

std::string Input::padName() const { return pad_ ? SDL_GetGamepadName(pad_) : "none"; }
