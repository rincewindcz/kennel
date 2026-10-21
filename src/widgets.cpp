#include "widgets.h"

#include <algorithm>
#include <cmath>

#include <sys/stat.h>

#include "image.h"
#include "paths.h"
#include "settings.h"
#include "util.h"

namespace {
constexpr float kBottom = 36;

size_t prevChar(const std::string& s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) --i;
    return i;
}

size_t nextChar(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && ((unsigned char)s[i] & 0xc0) == 0x80) ++i;
    return i;
}
}  // namespace


SDL_Texture* wallpaper() {
    static std::string loadedKey;
    static SDL_Texture* tex = nullptr;
    struct stat st;
    std::string path = paths.expand(settings.wallpaper);
    std::string key = settings.wallpaper.empty() || stat(path.c_str(), &st) != 0
                          ? std::string()
                          : strprintf("%s\t%lld\t%lld", path.c_str(), (long long)st.st_size, (long long)st.st_mtime);
    if (key == loadedKey) return tex;
    loadedKey = key;
    if (tex) SDL_DestroyTexture(tex);
    tex = nullptr;
    if (key.empty()) return nullptr;
    std::string qoi = paths.cache + "/wallpaper.qoi", keyFile = paths.cache + "/wallpaper.key";
    Image img;
    if (readFile(keyFile).value_or("") != key || !loadQoi(qoi, img)) {
        Image src;
        if (!loadImage(path, src)) {
            logf("wallpaper %s: cannot decode", path.c_str());
            return nullptr;
        }
        img = cover(src, (int)gfx.W, (int)gfx.H);
        if (saveQoi(qoi, img)) writeFile(keyFile, key);
    }
    tex = gfx.texture(img);
    return tex;
}

void drawBackground(float alpha, bool light) {
    if (settings.background == Settings::Background::Wallpaper) {
        if (SDL_Texture* w = wallpaper()) {
            gfx.fill({0, 0, gfx.W, gfx.H}, fade(theme::bgBottom, alpha));
            gfx.image(w, {0, 0, gfx.W, gfx.H}, alpha);
            gfx.gradient({0, 0, gfx.W, gfx.H}, rgb(0x0a0c11, (light ? 0.45f : 0.7f) * alpha),
                         rgb(0x0a0c11, (light ? 0.8f : 0.9f) * alpha));
            return;
        }
    }
    gfx.gradient({0, 0, gfx.W, gfx.H}, fade(theme::bgTop, alpha), fade(theme::bgBottom, alpha));
    gfx.glow({-260, -300, 700, 560}, accent(0.16f * alpha));
}

void drawPageHeader(const std::string& icon, const std::string& title, const std::string& subtitle, float alpha) {
    float x = 24, y = 42;
    gfx.round({x, y, 36, 36}, 10, accent(0.18f * alpha));
    gfx.icon(icon, x + 8, y + 8, 20, accent(alpha));
    gfx.text(Font::Title, title, x + 48, y - 2, fade(theme::text, alpha), gfx.W - 100);
    if (!subtitle.empty()) gfx.text(Font::Small, subtitle, x + 49, y + 28, fade(theme::dim, alpha), gfx.W - 100);
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
    bool used = menu_.input(ev);
    if (rebuildPending_) {
        rebuildPending_ = false;
        rebuild();
    }
    if (!used && extraKey) extraKey(ev.key);
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

// ------------------------------------------------------------ text input ---

TextInputScreen::TextInputScreen(std::string title, std::string text, std::function<void(const std::string&)> done)
    : title_(std::move(title)), text_(std::move(text)), cursor_(text_.size()), done_(std::move(done)) {}

void TextInputScreen::input(const KeyEvent& ev) {
    switch (ev.key) {
        case Key::Select: close(); return;
        case Key::Y:
            text_.clear();
            cursor_ = 0;
            return;
        case Key::L1: cursor_ = prevChar(text_, cursor_); return;
        case Key::R1: cursor_ = nextChar(text_, cursor_); return;
        default: break;
    }
    Keyboard::Result r = keyboard_.input(ev);
    switch (r.action) {
        case Keyboard::Action::Text:
            text_.insert(cursor_, r.text);
            cursor_ += r.text.size();
            break;
        case Keyboard::Action::Backspace: {
            size_t from = prevChar(text_, cursor_);
            text_.erase(from, cursor_ - from);
            cursor_ = from;
            break;
        }
        case Keyboard::Action::Enter: {
            auto done = done_;
            std::string text = text_;
            close();
            if (done) done(text);
            break;
        }
        default: break;
    }
}

std::vector<Hint> TextInputScreen::hints() const {
    return {{Key::Back, "Delete"}, {Key::X, "Space"}, {Key::Y, "Clear"}, {Key::Start, "Done"}, {Key::Select, "Cancel"}};
}

void TextInputScreen::draw() {
    float t = shown.value;
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0, 0.6f * t));
    float kh = keyboard_.height();
    float ky = gfx.H - kBottom - kh + 30 * (1 - t);
    SDL_FRect panel{16, 44, gfx.W - 32, 92};
    gfx.shadow(panel, 14, 18, rgb(0, 0.5f * t));
    gfx.round(panel, 14, fade(theme::panel, t));
    gfx.text(Font::SmallBold, title_, panel.x + 18, panel.y + 12, fade(theme::dim, t), panel.w - 36);
    SDL_FRect field{panel.x + 14, panel.y + 40, panel.w - 28, 38};
    gfx.round(field, 9, fade(rgb(0x07090d), 0.8f * t));
    gfx.stroke(field, 9, 1, accent(0.6f * t));

    // Keep the cursor visible: scroll the text left when it would pass the right edge.
    float maxW = field.w - 24;
    float before = gfx.measure(Font::Body, std::string_view(text_).substr(0, cursor_));
    float shift = std::max(0.f, before - maxW);
    gfx.pushClip({field.x + 8, field.y, field.w - 16, field.h});
    float tx = field.x + 12 - shift, ty = field.y + (field.h - gfx.lineHeight(Font::Body)) / 2;
    if (!text_.empty()) gfx.text(Font::Body, text_, tx, ty, fade(theme::text, t));
    gfx.fill({tx + before, field.y + 8, 2, field.h - 16}, accent(t));
    gfx.popClip();

    keyboard_.draw({0, ky, gfx.W, kh}, t);
}
