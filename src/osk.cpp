#include "osk.h"

#include <algorithm>
#include <cmath>

#include "menu.h"

namespace {
constexpr float kKeyH = 30, kTermKeyH = 26, kGap = 4, kPad = 6;

using Action = Keyboard::Action;

std::vector<std::string> chars(const char* s) {
    std::vector<std::string> out;
    for (const char* p = s; *p; ++p) out.emplace_back(1, *p);
    return out;
}
}  // namespace

Keyboard::Keyboard(bool terminalKeys) : terminal_(terminalKeys) {
    auto textRow = [](const char* s) {
        Row r;
        for (auto& c : chars(s)) r.push_back({c, c});
        return r;
    };
    auto key = [](std::string label, Action a, float w = 1, std::string icon = {}) {
        Def d;
        d.label = std::move(label);
        d.action = a;
        d.width = w;
        d.icon = std::move(icon);
        return d;
    };
    auto special = [](std::string label, Def::Special s, float w, std::string icon = {}) {
        Def d;
        d.label = std::move(label);
        d.special = s;
        d.width = w;
        d.icon = std::move(icon);
        return d;
    };
    auto space = [] {
        Def d{"space", " "};
        d.width = 5;
        return d;
    };
    auto bottom = [&](const char* layer, const char* left, const char* right) {
        Row r;
        r.push_back(special(layer, Def::Layer, 2));
        r.push_back({left, left});
        r.push_back(space());
        r.push_back({right, right});
        r.push_back(key("", Action::Enter, 2, "corner-down-left"));
        return r;
    };
    auto letters = [&](const char* r0, const char* r1, const char* r2, const char* r3, const char* l, const char* r) {
        std::vector<Row> rows{textRow(r0), textRow(r1), textRow(r2)};
        Row shiftRow;
        shiftRow.push_back(special("", Def::Shift, 1.5f, "arrow-big-up"));
        for (auto& d : textRow(r3)) shiftRow.push_back(d);
        shiftRow.push_back(key("", Action::Backspace, 1.5f, "delete"));
        rows.push_back(std::move(shiftRow));
        rows.push_back(bottom("?123", l, r));
        return rows;
    };
    layers_[kLower] = letters("1234567890-", "qwertyuiop/", "asdfghjkl.~", "zxcvbnm_", "|", "'");
    layers_[kUpper] = letters("!@#$%^&*()+", "QWERTYUIOP?", "ASDFGHJKL:>", "ZXCVBNM=", "&", "\"");
    std::vector<Row> sym{textRow("1234567890="), textRow("!@#$%^&*()+"), textRow("`~|\\/[]{}<>")};
    Row symLast = textRow(";:'\",.?-_");
    symLast.push_back(key("", Action::Backspace, 2, "delete"));
    sym.push_back(std::move(symLast));
    sym.push_back(bottom("abc", ",", "."));
    layers_[kSymbols] = std::move(sym);

    if (terminal_) {
        float w = 11.f / 12;
        Row t{key("Esc", Action::Esc, w), key("Tab", Action::Tab, w), special("Ctrl", Def::Ctrl, w),
              key("\xE2\x86\x90", Action::Left, w), key("\xE2\x86\x93", Action::Down, w), key("\xE2\x86\x91", Action::Up, w),
              key("\xE2\x86\x92", Action::Right, w), key("Home", Action::Home, w), key("End", Action::End, w),
              key("PgUp", Action::PageUp, w), key("PgDn", Action::PageDown, w), key("", Action::Menu, w, "settings")};
        for (auto& l : layers_) l.insert(l.begin(), t);
    }
    row_ = terminal_ ? 3 : 2;
    col_ = 4;
}

int Keyboard::layer() const { return symbols_ ? kSymbols : shifted() ? kUpper : kLower; }

const std::vector<Keyboard::Row>& Keyboard::rows() const { return layers_[layer()]; }

float Keyboard::height() const {
    return kPad * 2 + 5 * kKeyH + 4 * kGap + (terminal_ ? kTermKeyH + kGap : 0);
}

float Keyboard::centre(int row, int col) const {
    const Row& r = rows()[row];
    float x = 0;
    for (int i = 0; i < col; ++i) x += r[i].width;
    return x + r[col].width / 2;
}

void Keyboard::moveRow(int dir) {
    const auto& rs = rows();
    float x = centre(row_, col_);
    row_ = (row_ + dir + (int)rs.size()) % (int)rs.size();
    float best = 1e9f;
    for (int i = 0; i < (int)rs[row_].size(); ++i) {
        float d = std::fabs(centre(row_, i) - x);
        if (d < best) {
            best = d;
            col_ = i;
        }
    }
}

Keyboard::Result Keyboard::press(const Def& d) {
    Result r;
    r.handled = true;
    switch (d.special) {
        case Def::Shift:
            if (caps_) caps_ = shift_ = false;
            else if (shift_) caps_ = true, shift_ = false;
            else shift_ = true;
            return r;
        case Def::Layer:
            symbols_ = !symbols_;
            return r;
        case Def::Ctrl:
            ctrl_ = !ctrl_;
            return r;
        case Def::None: break;
    }
    r.action = d.action;
    r.text = d.text;
    r.ctrl = ctrl_;
    ctrl_ = false;
    if (d.action == Action::Text && shift_ && !symbols_) shift_ = false;
    return r;
}

Keyboard::Result Keyboard::input(const KeyEvent& ev) {
    Result r = handle(ev);
    col_ = std::min(col_, (int)rows()[row_].size() - 1);
    return r;
}

Keyboard::Result Keyboard::handle(const KeyEvent& ev) {
    Result r;
    const auto& rs = rows();
    switch (ev.key) {
        case Key::Up: moveRow(-1); break;
        case Key::Down: moveRow(1); break;
        case Key::Left: col_ = (col_ - 1 + (int)rs[row_].size()) % (int)rs[row_].size(); break;
        case Key::Right: col_ = (col_ + 1) % (int)rs[row_].size(); break;
        case Key::Confirm: return press(rs[row_][col_]);
        case Key::Back: return press({"", "", "", Action::Backspace});
        case Key::X: return press({" ", " "});
        case Key::Start: return press({"", "", "", Action::Enter});
        case Key::L2: {
            Def d;
            d.special = Def::Shift;
            symbols_ = false;
            return press(d);
        }
        case Key::R2: {
            Def d;
            d.special = Def::Layer;
            return press(d);
        }
        default: return r;
    }
    r.handled = true;
    return r;
}

void Keyboard::draw(const SDL_FRect& area, float alpha) {
    gfx.fill(area, fade(rgb(0x0b0d12), 0.96f * alpha));
    gfx.fill({area.x, area.y, area.w, 1}, fade(theme::line, alpha));
    const auto& rs = rows();
    float unit = (area.w - 2 * kPad) / 11.f;
    float y = area.y + kPad;
    for (int ri = 0; ri < (int)rs.size(); ++ri) {
        bool term = terminal_ && ri == 0;
        float h = term ? kTermKeyH : kKeyH;
        float x = area.x + kPad;
        for (int ci = 0; ci < (int)rs[ri].size(); ++ci) {
            const Def& d = rs[ri][ci];
            SDL_FRect k{x + kGap / 2, y, d.width * unit - kGap, h};
            x += d.width * unit;
            bool sel = ri == row_ && ci == col_;
            bool latched = (d.special == Def::Shift && shifted()) || (d.special == Def::Ctrl && ctrl_) ||
                           (d.special == Def::Layer && symbols_);
            Color bg = sel ? accent(alpha) : latched ? accent(0.35f * alpha) : fade(term || d.special != Def::None || d.action != Action::Text ? theme::panelHi : theme::panel, alpha);
            gfx.round(k, 7, bg);
            Color fg = fade(sel ? rgb(0x0b0d12) : theme::text, alpha);
            if (!d.icon.empty()) {
                float s = 18;
                gfx.icon(d.icon, k.x + (k.w - s) / 2, k.y + (k.h - s) / 2, s, fg);
                continue;
            }
            Font f = d.label.size() > 1 && d.label[0] != '\xE2' ? Font::Tiny : Font::Bold;
            gfx.text(f, d.label, k.x + k.w / 2, k.y + (k.h - gfx.lineHeight(f)) / 2, fg, 0, Align::Center);
        }
        y += h + kGap;
    }
}
