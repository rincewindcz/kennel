#include "anim.h"

#include <SDL3/SDL_timer.h>

namespace anim {
namespace {
double gNow = 0;
float gScale = 1;
bool gAlive = false;
}  // namespace
void tick() {
    gNow = SDL_GetTicksNS() / 1e9;
    gAlive = false;
}
double now() { return gNow; }
float scale() { return gScale; }
void setScale(float s) { gScale = s; }
void keepAlive() { gAlive = true; }
bool alive() { return gAlive; }
}  // namespace anim
