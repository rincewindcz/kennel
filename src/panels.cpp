#include "panels.h"

#include "platform.h"
#include "settings.h"
#include "system.h"
#include "util.h"

namespace {
constexpr float kPanelW = 320;
}

QuickPanel::QuickPanel() {
    menu_.compact = true;
    menu_.rowHeight = 38;
    auto& m = menu_.items;
    if (sys.hasBacklight())
        m.push_back(items::slider("sun", "Brightness", 5, 100, 5, [] { return sys.brightness(); },
                                  [](int v) { sys.setBrightness(v); }));
    if (sys.hasVolume())
        m.push_back(items::slider("volume-2", "Volume", 0, 100, 5, [] { return sys.volume(); }, [](int v) {
            sys.setVolume(v);
            if (sys.hasMute() && v > 0 && sys.muted()) sys.setMuted(false);
        }));
    if (sys.hasMute()) m.push_back(items::toggle("volume-x", "Mute", [] { return sys.muted(); }, [](bool v) { sys.setMuted(v); }));
    if (sys.hasOutputSwitch())
        m.push_back(items::choice("headphones", "Output", {"Speaker", "Headphones"}, [] { return sys.speaker() ? 0 : 1; },
                                  [](int v) { sys.setSpeaker(v == 0); }));
    m.push_back(items::header("GO TO"));
    m.push_back(items::link("chart-column", "Statistics", [] { app.openStatistics(); }));
    m.push_back(items::link("settings", "Settings", [] { app.openSettings(); }));
    if (settings.expert) m.push_back(items::link("terminal", "Expert", [] { app.openExpert(); }));
    if (sys.hasBacklight()) m.push_back(items::action("monitor-off", "Screen off", [this] {
        close();
        app.screenOff();
    }));
    menu_.fixSelection();
}

void QuickPanel::input(const KeyEvent& ev) {
    if (ev.key == Key::Back || ev.key == Key::Start) {
        close();
        return;
    }
    if (ev.key == Key::Confirm) {
        const MenuItem* m = menu_.current();
        if (m && m->type == MenuItem::Type::Link) close();
    }
    menu_.input(ev);
    app.dirty = true;
}

std::vector<Hint> QuickPanel::hints() const { return {{Key::Confirm, "Select"}, {Key::Back, "Close"}}; }

void QuickPanel::draw() {
    float t = shown.value;
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.5f * t));
    float x = gfx.W - kPanelW * t;
    SDL_FRect r{x, 0, kPanelW + 20, gfx.H};
    gfx.shadow(r, 2, 20, rgb(0, 0.6f * t));
    gfx.fill(r, fade(theme::panel, 0.98f));
    gfx.text(Font::Bold, "Quick settings", x + 22, 44, fade(theme::text, t));
    menu_.draw({x + 8, 76, kPanelW - 16, gfx.H - 76 - 44}, t);
}
