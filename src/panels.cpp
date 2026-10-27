#include "panels.h"

#include "battery.h"
#include "platform.h"
#include "settings.h"
#include "system.h"
#include "util.h"

namespace {
constexpr float kPanelW = 320;

int usbSwitching = -1;  // index of the mode being switched to, -1: none

// Index into platform.usbModes of the current mode; 0 when unknown.
int usbModeIndex() {
    if (usbSwitching >= 0) return usbSwitching;
    for (size_t i = 0; i < platform.usbModes.size(); ++i)
        if (platform.usbModes[i].value == sys.usbMode) return (int)i;
    return 0;
}
}  // namespace

// The switch runs in the background (the USB gadget restarts for a second or two); the row
// shows the new mode at once and ignores input until the switch is done.
MenuItem usbModeItem() {
    if (sys.usbMode.empty()) {
        int status = 0;
        std::string out = runCapture(platform.usbMode, &status);
        if (status == 0) sys.usbMode = trim(out);
    }
    std::vector<std::string> labels;
    for (const auto& m : platform.usbModes) labels.push_back(m.label);
    return items::choice("usb", "USB mode", labels, usbModeIndex, [](int i) {
        if (usbSwitching >= 0) return;
        usbSwitching = i;
        app.runAsync(replaceAll(platform.usbModeSet, "%m", shellQuote(platform.usbModes[i].value)),
                     [i](int status, const std::string& out) {
                         usbSwitching = -1;
                         const Platform::UsbMode& m = platform.usbModes[i];
                         if (status == 0) {
                             sys.usbMode = m.value;
                             app.toast("USB mode: " + m.label, m.icon.empty() ? "usb" : m.icon, theme::good);
                         } else {
                             app.toast("USB mode change failed: " + trim(out), "triangle-alert", theme::bad);
                         }
                     });
    });
}

MenuPanel::MenuPanel(std::string title, std::vector<MenuItem> items, bool closeOnAction)
    : title_(std::move(title)), closeOnAction_(closeOnAction) {
    menu_.compact = true;
    menu_.rowHeight = 38;
    menu_.items = std::move(items);
    menu_.fixSelection();
}

QuickPanel::QuickPanel() : MenuPanel("Quick settings", {}, false) {
    auto& m = menu_.items;
    if (sys.battery.capacity >= 0)
        m.push_back(items::info("battery-full", "Battery", [] { return describeEstimate(sys.battery, batteryLog.estimate(sys.battery)); }));
    if (sys.hasBacklight())
        m.push_back(items::level("sun", "Brightness", sys.maxBrightness(), [] { return sys.brightness(); },
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
    if (platform.hasUsbModes()) m.push_back(usbModeItem());
    m.push_back(items::header("GO TO"));
    m.push_back(items::link("folder", "Files", [] { app.openFiles(); }));
    m.push_back(items::link("chart-column", "Statistics", [] { app.openStatistics(); }));
    m.push_back(items::link("settings", "Settings", [] { app.openSettings(); }));
    if (settings.expert) {
        m.push_back(items::link("wrench", "Expert", [] { app.openExpert(); }));
        m.push_back(items::link("terminal", "Terminal", [] { app.openTerminal(); }));
    }
    if (sys.hasBacklight()) m.push_back(items::action("monitor-off", "Screen off", [this] {
        close();
        app.screenOff();
    }));
    menu_.fixSelection();
}

void MenuPanel::input(const KeyEvent& ev) {
    if (ev.key == Key::Back || ev.key == Key::Start) {
        close();
        return;
    }
    if (ev.key == Key::Confirm) {
        const MenuItem* m = menu_.current();
        if (m && (m->type == MenuItem::Type::Link || (closeOnAction_ && m->type == MenuItem::Type::Action))) close();
    }
    menu_.input(ev);
    app.dirty = true;
}

std::vector<Hint> MenuPanel::hints() const { return {{Key::Confirm, "Select"}, {Key::Back, "Close"}}; }

void MenuPanel::draw() {
    float t = shown.value;
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.5f * t));
    float x = gfx.W - kPanelW * t;
    SDL_FRect r{x, 0, kPanelW + 20, gfx.H};
    gfx.shadow(r, 2, 20, rgb(0, 0.6f * t));
    gfx.fill(r, fade(theme::panel, 0.98f));
    gfx.text(Font::Bold, title_, x + 22, 44, fade(theme::text, t), kPanelW - 44);
    menu_.draw({x + 8, 76, kPanelW - 16, gfx.H - 76 - 44}, t);
}
