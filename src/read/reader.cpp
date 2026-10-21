#include "reader.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdlib>

#include "anim.h"
#include "paths.h"
#include "platform.h"
#include "positions.h"
#include "settings.h"
#include "util.h"

namespace {
constexpr float kStatusH = 22;
constexpr float kTopPad = 12;
constexpr double kSaveDelay = 2.0;
constexpr float kPanelW = 320;
}  // namespace

// ------------------------------------------------------------------ setup ---

void Reader::opening(const std::string& name) {
    gfx.beginFrame();
    gfx.fill({0, 0, gfx.W, gfx.H}, rs_.background());
    gfx.text(Font::Small, "Opening", gfx.W / 2, gfx.H / 2 - 24, rs_.dim(), 0, Align::Center);
    gfx.text(Font::Bold, name, gfx.W / 2, gfx.H / 2 - 4, rs_.text(), gfx.W - 60, Align::Center);
    gfx.endFrame();
}

void Reader::apply(bool fonts) {
    uint64_t offset = book_.paragraphs.empty() ? 0 : layout_.offset(top_);
    if (fonts) {
        bool ok = glyphs_.open(rs_.family, rs_.size);
        if (!ok && rs_.family != "Inter") {
            rs_.family = "Inter";
            glyphs_.open(rs_.family, rs_.size);
        }
    }
    relayout();
    top_ = layout_.find(offset);
    dirty_ = true;
}

void Reader::relayout() {
    float margin = (float)rs_.marginPx();
    layout_.reset(&book_, &glyphs_, &rs_, gfx.W - 2 * margin);
}

float Reader::lineStep() const { return std::round(glyphs_.lineSkip() * rs_.spacing / 10.f); }

float Reader::textTop() const { return kTopPad; }

int Reader::visibleLines() const {
    float bottom = gfx.H - (rs_.statusBar ? kStatusH + 4 : kTopPad);
    return std::max(1, (int)((bottom - textTop()) / lineStep()));
}

// --------------------------------------------------------------- movement ---

bool Reader::atEnd() {
    LinePos p = top_;
    for (int i = 0; i < visibleLines(); ++i)
        if (!layout_.next(p)) return true;
    return false;
}

void Reader::scroll(int lines) {
    LinePos before = top_;
    for (int i = 0; i < lines && !atEnd(); ++i) layout_.next(top_);
    for (int i = 0; i > lines && layout_.prev(top_); --i) {
    }
    if (before.paragraph != top_.paragraph || before.line != top_.line) {
        dirty_ = true;
        posDirty_ = true;
        scrolledAt_ = anim::now();
    }
}

void Reader::jump(int paragraph) {
    top_ = {std::clamp(paragraph, 0, (int)book_.paragraphs.size() - 1), 0};
    const auto& ls = layout_.lines(top_.paragraph);
    if (ls.size() > 1 && ls[0].kind == Line::Kind::Blank) top_.line = 1;
    dirty_ = true;
    posDirty_ = true;
    scrolledAt_ = anim::now();
}

void Reader::chapterStep(int dir) {
    if (book_.chapters.empty()) return;
    int c = book_.chapterAt(top_.paragraph);
    if (dir < 0) {
        if (c >= 0 && ((int)book_.chapters[c].paragraph < top_.paragraph || top_.line > 0)) jump(book_.chapters[c].paragraph);
        else if (c > 0) jump(book_.chapters[c - 1].paragraph);
        else jump(0);
    } else if (c + 1 < (int)book_.chapters.size()) {
        jump(book_.chapters[c + 1].paragraph);
    }
}

void Reader::savePos() {
    if (book_.paragraphs.empty()) return;
    savePosition(bookPath_, layout_.offset(top_), book_.size);
    posDirty_ = false;
}

// ----------------------------------------------------------------- panels ---

void Reader::openChapters() {
    if (book_.chapters.empty()) return;
    menu_ = Menu{};
    menu_.compact = true;
    menu_.rowHeight = 34;
    int cur = std::max(0, book_.chapterAt(top_.paragraph));
    for (size_t i = 0; i < book_.chapters.size(); ++i) {
        int para = (int)book_.chapters[i].paragraph;
        auto item = items::action("", book_.chapters[i].title, [this, para] {
            jump(para);
            mode_ = Mode::Reading;
        });
        uint64_t off = book_.offsets[para];
        item.value = [this, off] { return strprintf("%d%%", (int)(off * 100 / std::max<uint64_t>(1, book_.size))); };
        menu_.items.push_back(std::move(item));
    }
    menu_.selected = cur;
    mode_ = Mode::Chapters;
}

int Reader::brightness() const {
    long v = 0;
    if (maxBrightness_ <= 0 || !readLong(platform.backlight + "/brightness", v)) return 0;
    return (int)std::lround(v * 100.0 / maxBrightness_);
}

void Reader::setBrightness(int percent) {
    if (maxBrightness_ <= 0) return;
    long raw = std::max(1L, std::lround(std::clamp(percent, 1, 100) * maxBrightness_ / 100.0));
    writeFile(platform.backlight + "/brightness", std::to_string(raw));
}

void Reader::openSettings() {
    menu_ = Menu{};
    menu_.compact = true;
    menu_.rowHeight = 36;
    auto& m = menu_.items;
    std::vector<std::string> families(kReadFamilies, kReadFamilies + kReadFamilyCount);
    m.push_back(items::choice("type", "Font", families,
                              [this, families] {
                                  auto it = std::find(families.begin(), families.end(), rs_.family);
                                  return it == families.end() ? 0 : (int)(it - families.begin());
                              },
                              [this, families](int v) {
                                  rs_.family = families[v];
                                  apply(true);
                              }));
    std::vector<std::string> sizes;
    for (int s = 14; s <= 36; ++s) sizes.push_back(std::to_string(s));
    m.push_back(items::choice("type", "Size", sizes, [this] { return rs_.size - 14; }, [this](int v) {
        rs_.size = 14 + v;
        apply(true);
    }));
    std::vector<std::string> spacings;
    for (int s = 10; s <= 20; ++s) spacings.push_back(strprintf("%d.%d", s / 10, s % 10));
    m.push_back(items::choice("arrow-up-down", "Line spacing", spacings, [this] { return rs_.spacing - 10; }, [this](int v) {
        rs_.spacing = 10 + v;
        apply(false);
    }));
    m.push_back(items::choice("move-vertical", "Margins", {"Narrow", "Normal", "Wide"}, [this] { return (int)rs_.margins; },
                              [this](int v) {
                                  rs_.margins = (ReadSettings::Margins)v;
                                  apply(false);
                              }));
    m.push_back(items::choice("list", "Alignment", {"Left", "Justified"}, [this] { return (int)rs_.align; }, [this](int v) {
        rs_.align = (ReadSettings::Align)v;
        apply(false);
    }));
    m.push_back(items::choice("code", "Formatting", {"Plain", "Styled"}, [this] { return (int)rs_.format; }, [this](int v) {
        rs_.format = (ReadSettings::Format)v;
        apply(false);
    }));
    m.push_back(items::choice("file-text", "Paragraphs", {"Indent", "Spacing"}, [this] { return (int)rs_.paragraphs; },
                              [this](int v) {
                                  rs_.paragraphs = (ReadSettings::Paragraphs)v;
                                  apply(false);
                              }));
    m.push_back(items::choice("palette", "Theme", {"Dark", "Sepia", "Light"}, [this] { return (int)rs_.theme; }, [this](int v) {
        rs_.theme = (ReadSettings::Theme)v;
        dirty_ = true;
    }));
    m.push_back(items::toggle("chart-column", "Status bar", [this] { return rs_.statusBar; }, [this](bool v) {
        rs_.statusBar = v;
        apply(false);
    }));
    if (maxBrightness_ > 0)
        m.push_back(items::slider("sun", "Brightness", 5, 100, 5, [this] { return brightness(); },
                                  [this](int v) { setBrightness(v); }));
    menu_.fixSelection();
    mode_ = Mode::Settings;
}

void Reader::closePanel() {
    if (mode_ == Mode::Settings) rs_.save();
    mode_ = Mode::Reading;
    dirty_ = true;
}

// ------------------------------------------------------------------ input ---

void Reader::input(const KeyEvent& ev) {
    static const bool debug = getenv("KENNEL_DEBUG_INPUT") != nullptr;
    if (debug) logf("key %d%s", (int)ev.key, ev.repeat ? " repeat" : "");
    dirty_ = true;
    switch (mode_) {
        case Mode::Error:
            if (ev.key == Key::Back || ev.key == Key::Confirm) running_ = false;
            return;
        case Mode::Chapters:
            if (ev.key == Key::Back || ev.key == Key::Select) closePanel();
            else menu_.input(ev);
            return;
        case Mode::Settings:
            if (ev.key == Key::Back || ev.key == Key::Start) closePanel();
            else menu_.input(ev);
            return;
        case Mode::Reading: break;
    }
    int page = std::max(1, visibleLines() - 1);
    switch (ev.key) {
        case Key::Down: scroll(1); break;
        case Key::Up: scroll(-1); break;
        case Key::Right:
        case Key::R1: scroll(page); break;
        case Key::Left:
        case Key::L1: scroll(-page); break;
        case Key::R2: chapterStep(1); break;
        case Key::L2: chapterStep(-1); break;
        case Key::Select: openChapters(); break;
        case Key::Start: openSettings(); break;
        case Key::Back: running_ = false; break;
        default: break;
    }
}

// ------------------------------------------------------------------- draw ---

void Reader::drawText() {
    float margin = (float)rs_.marginPx();
    float step = lineStep();
    float baseline0 = textTop() + std::round((step - glyphs_.lineSkip()) / 2) + glyphs_.ascent();
    Color c = rs_.text();
    LinePos p = top_;
    int n = visibleLines();
    for (int i = 0; i < n; ++i) {
        const auto& ls = layout_.lines(p.paragraph);
        if (p.line < (int)ls.size()) layout_.draw(p.paragraph, ls[p.line], margin, baseline0 + i * step, c);
        if (!layout_.next(p)) break;
    }
    glyphs_.flush(gfx.renderer);
}

void Reader::drawStatus() {
    if (!rs_.statusBar) return;
    float y = gfx.H - kStatusH;
    float margin = (float)rs_.marginPx();
    int ch = book_.chapterAt(top_.paragraph);
    std::string title = ch >= 0 ? book_.chapters[ch].title : book_.title;
    double frac = atEnd() ? 1.0 : (double)layout_.offset(top_) / std::max<uint64_t>(1, book_.size);
    std::string pct = strprintf("%d%%", (int)(frac * 100));
    float th = gfx.lineHeight(Font::Small);
    float pw = gfx.measure(Font::Small, pct);
    gfx.text(Font::Small, title, margin, y + (kStatusH - th) / 2 - 1, rs_.dim(), gfx.W - 2 * margin - pw - 16);
    gfx.text(Font::Small, pct, gfx.W - margin, y + (kStatusH - th) / 2 - 1, rs_.dim(), 0, Align::Right);
    gfx.fill({0, gfx.H - 2, gfx.W, 2}, fade(rs_.dim(), 0.25f));
    gfx.fill({0, gfx.H - 2, std::round(gfx.W * (float)frac), 2}, accent());
}

void Reader::drawHints(const std::vector<std::pair<Key, std::string>>& hints, Color bg) {
    float y = gfx.H - 34, bh = 20, x = 14;
    gfx.fill({0, y, gfx.W, 34}, bg);
    for (const auto& [key, label] : hints) {
        x += gfx.button(::input.label(key), x, y + 7, bh, rgb(0xe8ebf0, 0.92f), rgb(0x0b0d12)) + 6;
        x += gfx.text(Font::Small, label, x, y + (34 - gfx.lineHeight(Font::Small)) / 2, theme::dim) + 14;
    }
}

void Reader::drawChapters() {
    gfx.gradient({0, 0, gfx.W, gfx.H}, theme::bgTop, theme::bgBottom);
    gfx.text(Font::Title, "Contents", 24, 18, theme::text);
    gfx.text(Font::Small, book_.title, 24, 52, theme::dim, gfx.W - 48);
    menu_.draw({12, 80, gfx.W - 24, gfx.H - 80 - 40});
    drawHints({{Key::Confirm, "Go to"}, {Key::Back, "Back"}}, theme::bgBottom);
}

void Reader::drawSettings() {
    float x = gfx.W - kPanelW;
    gfx.fill({x, 0, kPanelW, gfx.H}, theme::panel);
    gfx.fill({x, 0, 1, gfx.H}, theme::line);
    gfx.text(Font::Bold, "Reading", x + 22, 18, theme::text);
    menu_.draw({x + 6, 52, kPanelW - 12, gfx.H - 52 - 40});
    float y = gfx.H - 34;
    gfx.fill({x, y, kPanelW, 34}, theme::panel);
    float hx = x + 14;
    for (const auto& [key, label] : std::vector<std::pair<Key, std::string>>{{Key::Left, "Change"}, {Key::Back, "Close"}}) {
        std::string l = key == Key::Left ? "\xE2\x97\x80\xE2\x96\xB6" : ::input.label(key);
        if (key == Key::Left) {
            hx += gfx.text(Font::SmallBold, "\xE2\x86\x94", hx, y + 8, theme::text) + 6;
        } else {
            hx += gfx.button(l, hx, y + 7, 20, rgb(0xe8ebf0, 0.92f), rgb(0x0b0d12)) + 6;
        }
        hx += gfx.text(Font::Small, label, hx, y + (34 - gfx.lineHeight(Font::Small)) / 2, theme::dim) + 14;
    }
}

void Reader::drawError() {
    gfx.fill({0, 0, gfx.W, gfx.H}, theme::bgBottom);
    gfx.icon("triangle-alert", gfx.W / 2 - 16, gfx.H / 2 - 80, 32, theme::warn);
    gfx.text(Font::Bold, "Cannot open the book", gfx.W / 2, gfx.H / 2 - 36, theme::text, 0, Align::Center);
    float y = gfx.H / 2 - 6;
    for (const auto& l : gfx.wrap(Font::Small, error_, gfx.W - 120, 4)) {
        gfx.text(Font::Small, l, gfx.W / 2, y, theme::dim, 0, Align::Center);
        y += gfx.lineHeight(Font::Small) + 2;
    }
    drawHints({{Key::Back, "Exit"}}, theme::bgBottom);
}

void Reader::draw() {
    gfx.beginFrame();
    if (mode_ == Mode::Error) {
        drawError();
    } else if (mode_ == Mode::Chapters) {
        drawChapters();
    } else {
        gfx.fill({0, 0, gfx.W, gfx.H}, rs_.background());
        drawText();
        drawStatus();
        if (mode_ == Mode::Settings) drawSettings();
    }
    if (!shot_.empty()) {
        if (SDL_Surface* surf = SDL_RenderReadPixels(gfx.renderer, nullptr)) {
            SDL_SaveBMP(surf, shot_.c_str());
            SDL_DestroySurface(surf);
        }
        shot_.clear();
    }
    gfx.endFrame();
}

// Development aid, as in kennel: KENNEL_AUTOMATE="down,wait:500,shot:/tmp/a.bmp,quit".
void Reader::automate(std::vector<KeyEvent>& keys) {
    if (script_.empty() || anim::now() < scriptAt_) return;
    std::string tok = script_.front();
    script_.erase(script_.begin());
    scriptAt_ = anim::now() + 0.3;
    static const std::pair<const char*, Key> names[] = {
        {"up", Key::Up}, {"down", Key::Down}, {"left", Key::Left}, {"right", Key::Right}, {"a", Key::Confirm},
        {"b", Key::Back}, {"x", Key::X}, {"y", Key::Y}, {"l1", Key::L1}, {"r1", Key::R1}, {"l2", Key::L2},
        {"r2", Key::R2}, {"start", Key::Start}, {"select", Key::Select}};
    if (tok.rfind("wait:", 0) == 0) scriptAt_ = anim::now() + atoi(tok.c_str() + 5) / 1000.0;
    else if (tok.rfind("shot:", 0) == 0) shot_ = tok.substr(5);
    else if (tok == "quit") running_ = false;
    else
        for (const auto& [n, k] : names)
            if (tok == n) keys.push_back({k, false});
    dirty_ = true;
}

// ------------------------------------------------------------------- main ---

int Reader::run(int argc, char** argv) {
    uint64_t t0 = SDL_GetTicksNS();
    paths.init(argv[0]);
    logToFile(paths.runtime + "/kennel-read.log");
    platform = detectPlatform();
    settings.load(paths.config + "/kennel.ini");
    rs_.load(paths.config + "/read.ini");
    anim::setScale(0);
    signal(SIGPIPE, SIG_IGN);
    if (argc < 2) {
        fprintf(stderr, "usage: kennel-read BOOK\n");
        return 2;
    }
    char resolved[PATH_MAX];
    bookPath_ = realpath(argv[1], resolved) ? resolved : argv[1];

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!gfx.init(platform.width, platform.height, platform.fullscreen)) return 1;
    if (!platform.backlight.empty()) readLong(platform.backlight + "/max_brightness", maxBrightness_);

    std::string name = baseName(bookPath_);
    opening(name.substr(0, name.rfind('.')));
    uint64_t tl = SDL_GetTicksNS();
    if (!loadBook(bookPath_, book_, error_)) {
        logf("%s: %s", bookPath_.c_str(), error_.c_str());
        mode_ = Mode::Error;
    } else {
        logf("%s: %zu paragraphs, %zu chapters, %llu bytes, parsed in %.0f ms", bookPath_.c_str(), book_.paragraphs.size(),
             book_.chapters.size(), (unsigned long long)book_.size, (SDL_GetTicksNS() - tl) / 1e6);
        if (!glyphs_.open(rs_.family, rs_.size)) {
            rs_.family = "Inter";
            if (!glyphs_.open(rs_.family, rs_.size)) {
                error_ = "no usable font";
                mode_ = Mode::Error;
            }
        }
        if (mode_ != Mode::Error) {
            relayout();
            top_ = layout_.find(loadPosition(bookPath_));
        }
    }

    if (const char* script = getenv("KENNEL_AUTOMATE")) script_ = split(script, ',');
    bool perf = getenv("KENNEL_PERF") != nullptr;
    bool gamepadStarted = false;
    double perfSum = 0;
    int perfFrames = 0;
    std::vector<KeyEvent> keys;

    while (running_) {
        anim::tick();
        int timeout = posDirty_ ? (int)std::max(0.0, (scrolledAt_ + kSaveDelay - anim::now()) * 1000) : -1;
        int repeat = ::input.timeoutMs();
        if (repeat >= 0) timeout = timeout < 0 ? repeat : std::min(timeout, repeat);
        if (!script_.empty()) timeout = timeout < 0 ? 50 : std::min(timeout, 50);
        if (dirty_ || !gamepadStarted) timeout = 0;
        SDL_Event ev;
        bool got = timeout < 0 ? SDL_WaitEvent(&ev) : SDL_WaitEventTimeout(&ev, timeout);
        keys.clear();
        while (got) {
            if (ev.type == SDL_EVENT_QUIT) running_ = false;
            else if (ev.type == SDL_EVENT_WINDOW_EXPOSED) dirty_ = true;
            ::input.handle(ev, keys);
            got = SDL_PollEvent(&ev);
        }
        anim::tick();
        ::input.tick(keys);
        automate(keys);
        for (const auto& k : keys) input(k);
        if (posDirty_ && anim::now() - scrolledAt_ >= kSaveDelay) savePos();

        if (dirty_ && running_) {
            dirty_ = false;
            uint64_t f0 = SDL_GetTicksNS();
            draw();
            if (perf) {
                perfSum += (SDL_GetTicksNS() - f0) / 1e6;
                if (++perfFrames == 60) {
                    logf("frame %.1f ms average over 60 frames, %zu glyphs, %d pages", perfSum / 60, glyphs_.glyphCount(),
                         glyphs_.pageCount());
                    perfSum = 0;
                    perfFrames = 0;
                }
            }
            if (!gamepadStarted) {
                gamepadStarted = true;
                logf("first text frame at %.0f ms", (SDL_GetTicksNS() - t0) / 1e6);
                flushBookCache();
                if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) logf("gamepad init: %s", SDL_GetError());
            }
        }
    }

    if (mode_ != Mode::Error) savePos();
    if (mode_ == Mode::Settings) rs_.save();
    glyphs_.close();
    gfx.shutdown();
    SDL_Quit();
    return 0;
}
