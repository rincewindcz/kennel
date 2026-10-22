#include "menu.h"

#include <algorithm>
#include <cmath>

#include "settings.h"
#include "util.h"

Color accent(float a) { return rgb(settings.accentColor(), a); }

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
MenuItem level(std::string icon, std::string label, int maxLevel, std::function<int()> get, std::function<void(int)> set) {
    MenuItem m = slider(std::move(icon), std::move(label), 1, std::max(1, maxLevel), std::max(1, maxLevel / 20), std::move(get),
                        std::move(set));
    m.format = [maxLevel](int v) { return strprintf("%d%%", (int)std::lround(v * 100.0 / std::max(1, maxLevel))); };
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
        } else if (m.indent) {
            x += 32;
        }
        Font lf = compact ? Font::Small : Font::Body;
        float labelMax = area.w * 0.55f;
        float labelEnd = x + gfx.text(lf, m.label, x, mid - gfx.lineHeight(lf) / 2, fade(fg, alpha), labelMax);

        float right = area.x + area.w - 24;
        switch (m.type) {
            case MenuItem::Type::Toggle: {
                bool on = m.getBool();
                SDL_FRect track{right - 38, mid - 11, 38, 22};
                gfx.round(track, 11, on ? accent(alpha) : fade(rgb(0x4a5263), alpha));
                gfx.round({on ? track.x + 19 : track.x + 3, track.y + 3, 16, 16}, 8, fade(rgb(0xffffff), alpha));
                break;
            }
            case MenuItem::Type::Choice: {
                std::string v = m.choices.empty() ? "" : m.choices[std::clamp(m.getIndex(), 0, (int)m.choices.size() - 1)];
                Color c = fade(sel ? theme::text : theme::dim, alpha);
                gfx.icon("chevron-right", right - 18, mid - 10, 20, fade(c, sel ? 1 : 0.5f));
                float valueMax = std::max(area.w * 0.35f, right - 44 - labelEnd - 16);
                float w = gfx.text(Font::Body, v, right - 22, mid - gfx.lineHeight(Font::Body) / 2, c, valueMax, Align::Right);
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
                float vw = gfx.text(Font::SmallBold, m.format ? m.format(v) : strprintf("%d%%", v), tx - 12,
                                    mid - gfx.lineHeight(Font::SmallBold) / 2, fade(theme::dim, alpha), 0, Align::Right);
                if (m.swatch) {
                    SDL_FRect sw{tx - 12 - vw - 24, mid - 8, 16, 16};
                    gfx.round(sw, 4, fade(m.swatch(), alpha));
                    gfx.stroke(sw, 4, 1, fade(theme::line, alpha));
                }
                break;
            }
            case MenuItem::Type::Link:
                gfx.icon("chevron-right", right - 18, mid - 10, 20, fade(theme::dim, alpha));
                if (m.value) {
                    Font vf = compact ? Font::Small : Font::Body;
                    gfx.text(vf, m.value(), right - 26, mid - gfx.lineHeight(vf) / 2, fade(theme::dim, alpha), 0, Align::Right);
                }
                break;
            case MenuItem::Type::Info:
            case MenuItem::Type::Action:
                if (m.value) {
                    Font vf = compact ? Font::Small : Font::Body;
                    float vw = gfx.text(vf, m.value(), right, mid - gfx.lineHeight(vf) / 2, fade(theme::dim, alpha),
                                        area.w - (x - area.x) - 40 - std::min(gfx.measure(lf, m.label), labelMax), Align::Right);
                    if (m.swatch) {
                        SDL_FRect sw{right - vw - 26, mid - 8, 16, 16};
                        gfx.round(sw, 4, fade(m.swatch(), alpha));
                        gfx.stroke(sw, 4, 1, fade(theme::line, alpha));
                    }
                }
                break;
            default: break;
        }
    }
    gfx.popClip();
    drawScrollbar({area.x + area.w - 6, area.y + 4, 3, area.h - 8}, scroll, content, area.h, alpha);
}

