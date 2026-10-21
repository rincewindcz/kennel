#include "widgets.h"

#include <algorithm>
#include <cmath>

#include "settings.h"
#include "util.h"

namespace {
constexpr float kBottom = 36;
}

Color accent(float a) { return rgb(settings.accentColor(), a); }

void drawBackground(float alpha) {
    gfx.gradient({0, 0, gfx.W, gfx.H}, fade(theme::bgTop, alpha), fade(theme::bgBottom, alpha));
    gfx.shadow({-120, -160, 420, 260}, 120, 120, accent(0.10f * alpha));
}

void drawPageHeader(const std::string& icon, const std::string& title, const std::string& subtitle, float alpha) {
    float x = 24, y = 42;
    gfx.round({x, y, 36, 36}, 10, accent(0.18f * alpha));
    gfx.icon(icon, x + 8, y + 8, 20, accent(alpha));
    gfx.text(Font::Title, title, x + 48, y - 2, fade(theme::text, alpha), gfx.W - 100);
    if (!subtitle.empty()) gfx.text(Font::Small, subtitle, x + 49, y + 28, fade(theme::dim, alpha), gfx.W - 100);
}

void drawScrollbar(const SDL_FRect& track, float offset, float content, float view, float alpha) {
    if (content <= view) return;
    float h = std::max(24.f, track.h * view / content);
    float y = track.y + (track.h - h) * std::clamp(offset / (content - view), 0.f, 1.f);
    gfx.round({track.x, track.y, track.w, track.h}, track.w / 2, fade(theme::line, 0.6f * alpha));
    gfx.round({track.x, y, track.w, h}, track.w / 2, fade(theme::dim, alpha));
}

// ----------------------------------------------------------------- items ---

namespace items {
MenuItem header(std::string label) {
    MenuItem m;
    m.type = MenuItem::Type::Header;
    m.label = std::move(label);
    return m;
}
MenuItem action(std::string icon, std::string label, std::function<void()> f, bool danger) {
    MenuItem m;
    m.icon = std::move(icon);
    m.label = std::move(label);
    m.activate = std::move(f);
    m.danger = danger;
    return m;
}
MenuItem link(std::string icon, std::string label, std::function<void()> f) {
    MenuItem m = action(std::move(icon), std::move(label), std::move(f));
    m.type = MenuItem::Type::Link;
    return m;
}
MenuItem info(std::string icon, std::string label, std::function<std::string()> value) {
    MenuItem m;
    m.type = MenuItem::Type::Info;
    m.icon = std::move(icon);
    m.label = std::move(label);
    m.value = std::move(value);
    return m;
}
MenuItem toggle(std::string icon, std::string label, std::function<bool()> get, std::function<void(bool)> set) {
    MenuItem m;
    m.type = MenuItem::Type::Toggle;
    m.icon = std::move(icon);
    m.label = std::move(label);
    m.getBool = std::move(get);
    m.setBool = std::move(set);
    return m;
}
MenuItem choice(std::string icon, std::string label, std::vector<std::string> choices, std::function<int()> get,
                std::function<void(int)> set) {
    MenuItem m;
    m.type = MenuItem::Type::Choice;
    m.icon = std::move(icon);
    m.label = std::move(label);
    m.choices = std::move(choices);
    m.getIndex = std::move(get);
    m.setIndex = std::move(set);
    return m;
}
MenuItem slider(std::string icon, std::string label, int min, int max, int step, std::function<int()> get,
                std::function<void(int)> set) {
    MenuItem m;
    m.type = MenuItem::Type::Slider;
    m.icon = std::move(icon);
    m.label = std::move(label);
    m.min = min;
    m.max = max;
    m.step = step;
    m.getInt = std::move(get);
    m.setInt = std::move(set);
    return m;
}
}  // namespace items

// ------------------------------------------------------------------ menu ---

const MenuItem* Menu::current() const {
    if (selected < 0 || selected >= (int)items.size()) return nullptr;
    return &items[selected];
}

void Menu::fixSelection() {
    if (items.empty()) {
        selected = 0;
        return;
    }
    selected = std::clamp(selected, 0, (int)items.size() - 1);
    for (int i = selected; i < (int)items.size(); ++i)
        if (items[i].selectable()) {
            selected = i;
            return;
        }
    for (int i = selected; i >= 0; --i)
        if (items[i].selectable()) {
            selected = i;
            return;
        }
}

void Menu::move(int dir, bool wrap) {
    int n = (int)items.size();
    for (int step = 1; step <= n; ++step) {
        int i = selected + dir * step;
        if (wrap) i = (i % n + n) % n;
        else if (i < 0 || i >= n) return;
        if (items[i].selectable()) {
            selected = i;
            return;
        }
    }
}

bool Menu::input(const KeyEvent& ev) {
    if (items.empty()) return false;
    MenuItem* m = selected >= 0 && selected < (int)items.size() ? &items[selected] : nullptr;
    switch (ev.key) {
        case Key::Up: move(-1, !ev.repeat); return true;
        case Key::Down: move(1, !ev.repeat); return true;
        case Key::L1:
            for (int i = 0; i < 6; ++i) move(-1, false);
            return true;
        case Key::R1:
            for (int i = 0; i < 6; ++i) move(1, false);
            return true;
        case Key::Left:
        case Key::Right: {
            if (!m) return false;
            int dir = ev.key == Key::Left ? -1 : 1;
            if (m->type == MenuItem::Type::Choice && !m->choices.empty()) {
                int n = (int)m->choices.size();
                m->setIndex(((m->getIndex() + dir) % n + n) % n);
                return true;
            }
            if (m->type == MenuItem::Type::Slider) {
                m->setInt(std::clamp(m->getInt() + dir * m->step, m->min, m->max));
                return true;
            }
            if (m->type == MenuItem::Type::Toggle) {
                m->setBool(dir > 0);
                return true;
            }
            return false;
        }
        case Key::Confirm:
            if (!m) return false;
            if (m->type == MenuItem::Type::Toggle) m->setBool(!m->getBool());
            else if (m->type == MenuItem::Type::Choice && !m->choices.empty())
                m->setIndex((m->getIndex() + 1) % (int)m->choices.size());
            else if (m->activate && !ev.repeat) m->activate();
            return true;
        default: return false;
    }
}

void Menu::draw(const SDL_FRect& area, float alpha) {
    fixSelection();
    float rh = rowHeight;
    float content = items.size() * rh;
    float targetScroll = std::clamp(selected * rh + rh / 2 - area.h / 2, 0.f, std::max(0.f, content - area.h));
    if (!placed_) {
        scroll_.set(targetScroll);
        cursor_.set(selected * rh);
        placed_ = true;
    }
    scroll_.go(targetScroll, 0.18f);
    cursor_.go(selected * rh, 0.14f);
    float scroll = scroll_.update(), cy = cursor_.update();

    gfx.pushClip(area);
    const MenuItem* cur = current();
    if (cur && cur->selectable()) {
        SDL_FRect sel{area.x, area.y + cy - scroll + 2, area.w - 10, rh - 4};
        gfx.round(sel, 10, accent(0.16f * alpha));
        gfx.round({sel.x, sel.y + 9, 3, sel.h - 18}, 1.5f, accent(alpha));
    }

    for (int i = 0; i < (int)items.size(); ++i) {
        float y = area.y + i * rh - scroll;
        if (y + rh < area.y || y > area.y + area.h) continue;
        MenuItem& m = items[i];
        bool sel = i == selected;
        float mid = y + rh / 2;
        if (m.type == MenuItem::Type::Header) {
            gfx.text(Font::Tiny, m.label, area.x + 14, y + rh - gfx.lineHeight(Font::Tiny) - 6, fade(theme::faint, alpha));
            continue;
        }
        Color fg = m.danger ? theme::bad : theme::text;
        float x = area.x + 14;
        if (!m.icon.empty()) {
            gfx.icon(m.icon, x, mid - 10, 20, fade(sel ? accent() : m.danger ? theme::bad : theme::dim, alpha));
            x += 32;
        }
        Font lf = compact ? Font::Small : Font::Body;
        float labelMax = area.w * 0.55f;
        gfx.text(lf, m.label, x, mid - gfx.lineHeight(lf) / 2, fade(fg, alpha), labelMax);

        float right = area.x + area.w - 24;
        switch (m.type) {
            case MenuItem::Type::Toggle: {
                bool on = m.getBool();
                SDL_FRect track{right - 38, mid - 11, 38, 22};
                gfx.round(track, 11, on ? accent(alpha) : fade(theme::line, alpha));
                gfx.round({on ? track.x + 19 : track.x + 3, track.y + 3, 16, 16}, 8, fade(rgb(0xffffff), alpha));
                break;
            }
            case MenuItem::Type::Choice: {
                std::string v = m.choices.empty() ? "" : m.choices[std::clamp(m.getIndex(), 0, (int)m.choices.size() - 1)];
                Color c = fade(sel ? theme::text : theme::dim, alpha);
                gfx.icon("chevron-right", right - 18, mid - 10, 20, fade(c, sel ? 1 : 0.5f));
                float w = gfx.text(Font::Body, v, right - 22, mid - gfx.lineHeight(Font::Body) / 2, c, area.w * 0.35f, Align::Right);
                gfx.icon("chevron-left", right - 22 - w - 22, mid - 10, 20, fade(c, sel ? 1 : 0.5f));
                break;
            }
            case MenuItem::Type::Slider: {
                int v = m.getInt();
                float tw = 110, tx = right - tw;
                float f = (float)(v - m.min) / std::max(1, m.max - m.min);
                gfx.round({tx, mid - 3, tw, 6}, 3, fade(theme::line, alpha));
                gfx.round({tx, mid - 3, std::max(6.f, tw * f), 6}, 3, accent(alpha));
                gfx.round({tx + tw * f - 7, mid - 7, 14, 14}, 7, fade(rgb(0xffffff), alpha));
                gfx.text(Font::SmallBold, strprintf("%d%%", v), tx - 12, mid - gfx.lineHeight(Font::SmallBold) / 2,
                         fade(theme::dim, alpha), 0, Align::Right);
                break;
            }
            case MenuItem::Type::Link: gfx.icon("chevron-right", right - 18, mid - 10, 20, fade(theme::dim, alpha)); break;
            case MenuItem::Type::Info:
            case MenuItem::Type::Action:
                if (m.value) {
                    Font vf = compact ? Font::Small : Font::Body;
                    gfx.text(vf, m.value(), right, mid - gfx.lineHeight(vf) / 2, fade(theme::dim, alpha), area.w - (x - area.x) - 40 -
                             std::min(gfx.measure(lf, m.label), labelMax), Align::Right);
                }
                break;
            default: break;
        }
    }
    gfx.popClip();
    drawScrollbar({area.x + area.w - 6, area.y + 4, 3, area.h - 8}, scroll, content, area.h, alpha);
}

// ------------------------------------------------------------ menu screen ---

MenuScreen::MenuScreen(std::string title, std::string icon, std::function<std::vector<MenuItem>()> build)
    : title_(std::move(title)), icon_(std::move(icon)), build_(std::move(build)) {
    rebuild();
}

void MenuScreen::rebuild() {
    int keep = menu_.selected;
    menu_.items = build_();
    menu_.selected = keep;
    menu_.fixSelection();
}

void MenuScreen::resumed() { rebuild(); }

void MenuScreen::input(const KeyEvent& ev) {
    if (ev.key == Key::Back) {
        close();
        return;
    }
    if (menu_.input(ev)) return;
    if (extraKey) extraKey(ev.key);
}

std::vector<Hint> MenuScreen::hints() const {
    std::vector<Hint> h;
    const MenuItem* m = menu_.current();
    if (m) {
        switch (m->type) {
            case MenuItem::Type::Toggle: h.push_back({Key::Confirm, "Toggle"}); break;
            case MenuItem::Type::Choice: h.push_back({Key::Confirm, "Change"}); break;
            case MenuItem::Type::Slider: break;
            default:
                if (m->activate) h.push_back({Key::Confirm, "Select"});
        }
    }
    h.push_back({Key::Back, "Back"});
    for (const auto& e : extraHints) h.push_back(e);
    return h;
}

void MenuScreen::draw() {
    float t = shown.value;
    float dx = 24 * (1 - t);
    drawBackground(t);
    drawPageHeader(icon_, title_, subtitle, t);
    SDL_FRect area{12 + dx, 92, gfx.W - 24, gfx.H - 92 - kBottom - 6};
    menu_.draw(area, t);
}

// ---------------------------------------------------------------- dialog ---

DialogScreen::DialogScreen(std::string icon, std::string title, std::string message, std::vector<DialogButton> buttons,
                           int selected)
    : icon_(std::move(icon)), title_(std::move(title)), message_(std::move(message)), buttons_(std::move(buttons)),
      selected_(selected) {
    if (buttons_.empty()) buttons_.push_back({"OK", nullptr});
    selected_ = std::clamp(selected_, 0, (int)buttons_.size() - 1);
}

void DialogScreen::choose(int i) {
    auto action = buttons_[i].action;
    close();
    if (action) action();
}

void DialogScreen::input(const KeyEvent& ev) {
    switch (ev.key) {
        case Key::Left: selected_ = std::max(0, selected_ - 1); break;
        case Key::Right: selected_ = std::min((int)buttons_.size() - 1, selected_ + 1); break;
        case Key::Confirm:
            if (!ev.repeat) choose(selected_);
            break;
        case Key::Back: close(); break;
        default: break;
    }
}

std::vector<Hint> DialogScreen::hints() const { return {{Key::Confirm, "Select"}, {Key::Back, "Cancel"}}; }

void DialogScreen::draw() {
    float t = shown.value;
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.6f * t));
    float w = 420, pad = 22;
    auto lines = gfx.wrap(Font::Small, message_, w - 2 * pad, 6);
    auto titleLines = gfx.wrap(Font::Bold, title_, w - 2 * pad - 52, 2);
    float h = pad + std::max(40.f, titleLines.size() * gfx.lineHeight(Font::Bold)) + 12 +
              lines.size() * (gfx.lineHeight(Font::Small) + 2) + 20 + 40 + pad;
    float s = 0.94f + 0.06f * t;
    float sw = w * s, sh = h * s;
    SDL_FRect r{(gfx.W - sw) / 2, (gfx.H - sh) / 2 - 10, sw, sh};
    gfx.shadow(r, 16, 24, rgb(0, 0.6f * t));
    gfx.round(r, 16, fade(theme::panel, t));
    gfx.stroke(r, 16, 1, fade(theme::line, t));

    float x = r.x + pad, y = r.y + pad;
    gfx.round({x, y, 40, 40}, 20, accent(0.18f * t));
    gfx.icon(icon_, x + 10, y + 10, 20, accent(t));
    float ty = y + (titleLines.size() == 1 ? 9 : 0);
    for (const auto& l : titleLines) {
        gfx.text(Font::Bold, l, x + 52, ty, fade(theme::text, t));
        ty += gfx.lineHeight(Font::Bold);
    }
    y += std::max(40.f, titleLines.size() * gfx.lineHeight(Font::Bold)) + 12;
    for (const auto& l : lines) {
        gfx.text(Font::Small, l, x, y, fade(theme::dim, t));
        y += gfx.lineHeight(Font::Small) + 2;
    }
    y = r.y + r.h - pad - 40;
    float bw = (r.w - 2 * pad - (buttons_.size() - 1) * 10) / buttons_.size();
    for (size_t i = 0; i < buttons_.size(); ++i) {
        SDL_FRect b{x + i * (bw + 10), y, bw, 40};
        bool sel = (int)i == selected_;
        Color bg = sel ? (buttons_[i].danger ? theme::bad : accent()) : theme::panelHi;
        gfx.round(b, 12, fade(bg, t));
        gfx.text(Font::Bold, buttons_[i].label, b.x + b.w / 2, b.y + (40 - gfx.lineHeight(Font::Bold)) / 2,
                 fade(sel ? rgb(0x0b0d12) : theme::text, t), bw - 16, Align::Center);
    }
}
