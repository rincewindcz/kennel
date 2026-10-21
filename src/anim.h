#pragma once

#include <algorithm>
#include <cmath>

// Frame clock and tweens. Durations are multiplied by the animation speed setting; a tween with a
// zero duration jumps. Anything still moving calls keepAlive() so the main loop renders another frame.
namespace anim {

void tick();            // once per frame
double now();           // seconds
float scale();          // animation speed factor (0 = off)
void keepAlive();
bool alive();           // keepAlive() called since the last tick
void setScale(float s);

inline float outCubic(float t) {
    t = 1 - t;
    return 1 - t * t * t;
}
inline float inOutQuad(float t) { return t < 0.5f ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2.f) / 2; }

struct Tween {
    float from = 0, to = 0, value = 0;
    double start = 0;
    float duration = 0;
    float (*ease)(float) = outCubic;

    void set(float v) {
        from = to = value = v;
        duration = 0;
    }
    void go(float target, float seconds) {
        if (target == to && duration > 0) return;
        from = value;
        to = target;
        start = now();
        duration = seconds * scale();
        if (duration <= 0) value = to;
        else keepAlive();
    }
    float update() {
        if (duration <= 0 || value == to) {
            value = to;
            return value;
        }
        float t = (float)std::clamp((now() - start) / duration, 0.0, 1.0);
        value = from + (to - from) * ease(t);
        if (t < 1) keepAlive();
        else value = to;
        return value;
    }
    bool moving() const { return value != to; }
};

}  // namespace anim
