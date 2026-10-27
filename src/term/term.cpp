#include "term.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>

#include "anim.h"
#include "ini.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "util.h"

namespace {

constexpr float kStripH = 28, kBarH = 24;
constexpr float kPanelW = 300;
constexpr int kMinFont = 10, kMaxFont = 20;
constexpr int kStickDead = 12000, kStickFast = 28000;
constexpr int kInputPollMs = 10;  // waitEvent() interval

const uint32_t kAnsi[16] = {0x20242d, 0xe06c75, 0x98c379, 0xe5c07b, 0x61afef, 0xc678dd, 0x56b6c2, 0xdcdfe4,
                            0x5c6370, 0xff7a85, 0xb5e890, 0xffd68a, 0x7cc4ff, 0xde9bf0, 0x6fd3df, 0xffffff};
const Color kFg = rgb(0xe6e9ef), kBg = rgb(0x0b0d12);

Color colour(uint32_t c, bool fg) {
    if (c == Vt::kDefault) return fg ? kFg : kBg;
    if (c & Vt::kRgb) return rgb(c & 0xffffff);
    if (c < 16) return rgb(kAnsi[c]);
    if (c < 232) {
        int i = (int)c - 16;
        auto level = [](int v) { return v ? 55 + v * 40 : 0; };
        return rgb((uint32_t)(level(i / 36) << 16 | level(i / 6 % 6) << 8 | level(i % 6)));
    }
    int g = 8 + ((int)c - 232) * 10;
    return rgb((uint32_t)(g << 16 | g << 8 | g));
}

size_t lastCharStart(const std::string& s) {
    size_t i = s.size();
    if (i == 0) return 0;
    --i;
    while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80) --i;
    return i;
}

const char* kHelp[] = {
    "TYPING (keyboard shown)",
    "  D-pad       move on the keyboard",
    "  A           press the key          B   backspace",
    "  X           space                  START  enter",
    "  L2          shift, twice for caps  R2  symbols",
    "  L1 / R1     choose a suggestion    Y   insert it",
    "  SELECT      keys mode",
    "",
    "KEYS MODE (for top, less, vi, alsamixer)",
    "  D-pad       arrow keys             A   enter",
    "  B           escape                 X   space",
    "  Y           tab                    L1 / R1  page up / down",
    "  L2          Ctrl-C                 R2  q",
    "  START       menu                   SELECT  keyboard",
    "",
    "Right stick or the menu's Scroll back: scrollback.",
    "Gear key on the keyboard: menu with font size and Ctrl keys.",
};

}  // namespace

// --------------------------------------------------------------- setup ---

void Term::loadSettings() {
    settingsPath_ = paths.config + "/term.ini";
    IniFile ini;
    if (loadIni(settingsPath_, ini))
        fontSize_ = std::clamp(atoi(ini.get("term", "font_size", std::to_string(fontSize_)).c_str()), kMinFont, kMaxFont);
}

void Term::saveSettings() { writeFileAtomic(settingsPath_, strprintf("[term]\nfont_size=%d\n", fontSize_)); }

bool Term::openFont() {
    if (!glyphs_.open("JetBrainsMono", fontSize_)) return false;
    cellW_ = std::max(1, glyphs_.glyph(Glyphs::Regular, 'M').advance);
    cellH_ = std::max(1, glyphs_.lineSkip());
    return true;
}

void Term::layout() {
    float bottom = gfx.H;
    Mode m = mode_ == Mode::Menu || mode_ == Mode::Help ? under_ : mode_;
    if (m == Mode::Type) bottom -= keyboard_.height() + kStripH;
    else bottom -= kBarH;
    int cols = std::max(10, (int)(gfx.W / cellW_));
    int rows = std::max(3, (int)((bottom - 4) / cellH_));
    grid_ = {std::floor((gfx.W - cols * cellW_) / 2), 2, (float)cols * cellW_, (float)rows * cellH_};
    if (cols != vt_.cols() || rows != vt_.rows()) {
        vt_.resize(cols, rows);
        pty_.resize(cols, rows);
    }
    scroll_ = std::min(scroll_, vt_.historySize());
    dirty_ = true;
}

void Term::setMode(Mode m) {
    if (m != Mode::Menu && m != Mode::Help) under_ = m;
    mode_ = m;
    layout();
}

// --------------------------------------------------------------- output ---

void Term::send(std::string_view bytes) {
    if (pty_.ended()) return;
    scroll_ = 0;
    pty_.write(bytes);
    dirty_ = true;
}

void Term::checkForeground() {
    bool fg = command_.empty() && pty_.shellInForeground();
    if (fg != shellForeground_) {
        shellForeground_ = fg;
        if (fg) {
            line_.clear();
            lineKnown_ = true;
        }
        suggestionsDirty_ = true;
        dirty_ = true;
    }
}

void Term::output() {
    std::string data = pty_.take();
    if (!data.empty()) {
        vt_.feed(data);
        std::string reply = vt_.takeReply();
        if (!reply.empty()) pty_.write(reply);
        if (scroll_ > 0) scroll_ = std::min(scroll_, vt_.historySize());
        dirty_ = true;
    }
    checkForeground();
    if (pty_.ended() && mode_ != Mode::Exited) {
        exitCode_ = pty_.exitStatus();
        if (command_.empty()) running_ = false;
        else setMode(Mode::Exited);
    }
}

void Term::type(const std::string& text, bool ctrl) {
    if (ctrl && text.size() == 1) {
        char c = text[0];
        char code = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 1)
                    : c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 1)
                    : c == '@' || c == ' ' ? 0
                    : c == '?' ? 0x7f
                    : c >= '[' && c <= '_' ? (char)(c - '[' + 0x1b)
                    : c;
        send(std::string_view(&code, 1));
        if (code == 0x03 || code == 0x15) {
            line_.clear();
            lineKnown_ = true;
        } else if (code != 0x0c) {  // Ctrl-L only redraws the line
            lineKnown_ = false;
        }
        suggestionsDirty_ = true;
        return;
    }
    send(text);
    if (shellForeground_) {
        line_ += text;
        suggestionsDirty_ = true;
    }
}

void Term::sendAction(Keyboard::Action a, bool ctrl) {
    using A = Keyboard::Action;
    bool app = vt_.appCursor();
    auto arrow = [&](char c) {
        if (ctrl) return std::string("\x1b[1;5") + c;
        return std::string(app ? "\x1bO" : "\x1b[") + c;
    };
    switch (a) {
        case A::Backspace:
            send("\x7f");
            if (shellForeground_ && !line_.empty()) line_.erase(lastCharStart(line_));
            break;
        case A::Enter:
            send("\r");
            if (shellForeground_ && lineKnown_) completer_.remember(line_);
            line_.clear();
            lineKnown_ = true;
            break;
        case A::Esc: send("\x1b"); break;
        case A::Tab: send("\t"); break;
        case A::Up: send(arrow('A')); break;
        case A::Down: send(arrow('B')); break;
        case A::Right: send(arrow('C')); break;
        case A::Left: send(arrow('D')); break;
        case A::Home: send(app ? "\x1bOH" : "\x1b[H"); break;
        case A::End: send(app ? "\x1bOF" : "\x1b[F"); break;
        case A::PageUp: send("\x1b[5~"); break;
        case A::PageDown: send("\x1b[6~"); break;
        case A::Menu: openMenu(); return;
        default: return;
    }
    if (a != A::Backspace && a != A::Enter) lineKnown_ = false;
    suggestionsDirty_ = true;
}

void Term::accept() {
    if (suggestions_.empty()) return;
    const Suggestion& s = suggestions_[std::min(suggestion_, (int)suggestions_.size() - 1)];
    std::string append = s.append;
    send(append);
    line_ += append;
    suggestion_ = 0;
    suggestionsDirty_ = true;
}

// ---------------------------------------------------------------- input ---

void Term::scrollBy(int lines) {
    int before = scroll_;
    scroll_ = vt_.altScreen() ? 0 : std::clamp(scroll_ + lines, 0, vt_.historySize());
    if (scroll_ != before) dirty_ = true;
}

void Term::input(const KeyEvent& ev) {
    static const bool debug = getenv("KENNEL_DEBUG_INPUT") != nullptr;
    if (debug) logf("key %d%s", (int)ev.key, ev.repeat ? " repeat" : "");
    dirty_ = true;
    switch (mode_) {
        case Mode::Type: inputType(ev); break;
        case Mode::Keys: inputKeys(ev); break;
        case Mode::Scroll: inputScroll(ev); break;
        case Mode::Help:
            if (ev.key == Key::Back || ev.key == Key::Confirm || ev.key == Key::Start) setMode(under_);
            break;
        case Mode::Menu:
            if (ev.key == Key::Back || ev.key == Key::Start) {
                setMode(under_);
                break;
            }
            if (ev.key == Key::Confirm) {
                const MenuItem* m = menu_.current();
                if (m && m->type == MenuItem::Type::Action) setMode(under_);
            }
            menu_.input(ev);
            break;
        case Mode::Exited:
            if (!ev.repeat) running_ = false;
            break;
    }
}

void Term::inputType(const KeyEvent& ev) {
    switch (ev.key) {
        case Key::Select: setMode(Mode::Keys); return;
        case Key::Y: accept(); return;
        case Key::L1:
            suggestion_ = std::max(0, suggestion_ - 1);
            return;
        case Key::R1:
            suggestion_ = std::min(std::max(0, (int)suggestions_.size() - 1), suggestion_ + 1);
            return;
        default: break;
    }
    Keyboard::Result r = keyboard_.input(ev);
    if (!r.handled) return;
    if (r.action == Keyboard::Action::Text) type(r.text, r.ctrl);
    else if (r.action != Keyboard::Action::None) sendAction(r.action, r.ctrl);
}

void Term::inputKeys(const KeyEvent& ev) {
    using A = Keyboard::Action;
    switch (ev.key) {
        case Key::Up: sendAction(A::Up, false); break;
        case Key::Down: sendAction(A::Down, false); break;
        case Key::Left: sendAction(A::Left, false); break;
        case Key::Right: sendAction(A::Right, false); break;
        case Key::Confirm: sendAction(A::Enter, false); break;
        case Key::Back: sendAction(A::Esc, false); break;
        case Key::X: type(" ", false); break;
        case Key::Y: sendAction(A::Tab, false); break;
        case Key::L1: sendAction(A::PageUp, false); break;
        case Key::R1: sendAction(A::PageDown, false); break;
        case Key::L2: type("c", true); break;
        case Key::R2: type("q", false); break;
        case Key::Start: openMenu(); break;
        case Key::Select: setMode(Mode::Type); break;
        default: break;
    }
}

void Term::inputScroll(const KeyEvent& ev) {
    int page = std::max(1, vt_.rows() - 1);
    switch (ev.key) {
        case Key::Up: scrollBy(1); break;
        case Key::Down: scrollBy(-1); break;
        case Key::L1:
        case Key::Left: scrollBy(page); break;
        case Key::R1:
        case Key::Right: scrollBy(-page); break;
        case Key::L2: scrollBy(INT_MAX / 2); break;
        case Key::R2: scrollBy(-INT_MAX / 2); break;
        case Key::Back:
        case Key::Select:
        case Key::Start:
            scroll_ = 0;
            setMode(under_ == Mode::Scroll ? Mode::Type : under_);
            break;
        default: break;
    }
}

// A USB keyboard types directly.
void Term::keyboard(const SDL_KeyboardEvent& k) {
    if (mode_ != Mode::Type && mode_ != Mode::Keys) return;
    using A = Keyboard::Action;
    bool ctrl = k.mod & SDL_KMOD_CTRL;
    switch (k.key) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER: sendAction(A::Enter, false); return;
        case SDLK_BACKSPACE: sendAction(A::Backspace, false); return;
        case SDLK_TAB: sendAction(A::Tab, false); return;
        case SDLK_ESCAPE: sendAction(A::Esc, false); return;
        case SDLK_UP: sendAction(A::Up, ctrl); return;
        case SDLK_DOWN: sendAction(A::Down, ctrl); return;
        case SDLK_LEFT: sendAction(A::Left, ctrl); return;
        case SDLK_RIGHT: sendAction(A::Right, ctrl); return;
        case SDLK_HOME: sendAction(A::Home, false); return;
        case SDLK_END: sendAction(A::End, false); return;
        case SDLK_PAGEUP: sendAction(A::PageUp, false); return;
        case SDLK_PAGEDOWN: sendAction(A::PageDown, false); return;
        case SDLK_DELETE:
            send("\x1b[3~");
            lineKnown_ = false;
            return;
        default: break;
    }
    if (ctrl && k.key >= SDLK_A && k.key <= SDLK_Z) type(std::string(1, (char)('a' + (k.key - SDLK_A))), true);
}

void Term::openMenu() {
    menu_ = Menu{};
    menu_.compact = true;
    menu_.rowHeight = 36;
    auto& m = menu_.items;
    std::vector<std::string> sizes;
    for (int s = kMinFont; s <= kMaxFont; ++s) sizes.push_back(std::to_string(s));
    m.push_back(items::choice("type", "Font size", sizes, [this] { return fontSize_ - kMinFont; }, [this](int v) {
        int old = fontSize_;
        fontSize_ = kMinFont + v;
        if (!openFont()) {
            fontSize_ = old;
            openFont();
        }
        saveSettings();
        layout();
    }));
    auto key = [&](const char* label, const char* letter) {
        std::string l = letter;
        m.push_back(items::action("keyboard", label, [this, l] { type(l, true); }));
    };
    key("Ctrl-C  interrupt", "c");
    key("Ctrl-D  end of input", "d");
    key("Ctrl-Z  suspend", "z");
    key("Ctrl-L  redraw", "l");
    m.push_back(items::action("arrow-up-down", "Scroll back", [this] { under_ = Mode::Scroll; }));
    m.push_back(items::action("trash-2", "Clear scrollback", [this] {
        vt_.clearHistory();
        scroll_ = 0;
    }));
    m.push_back(items::action("circle-help", "Controls", [this] { under_ = Mode::Help; }));
    m.push_back(items::action("x", "Close terminal", [this] {
        pty_.hangup();
        running_ = false;
    }, true));
    menu_.fixSelection();
    setMode(Mode::Menu);
}

// ----------------------------------------------------------------- draw ---

void Term::drawGrid() {
    const int cols = vt_.cols(), rows = vt_.rows();
    const float ox = grid_.x, oy = grid_.y;
    const bool cursor = scroll_ == 0 && vt_.cursorVisible() && mode_ != Mode::Scroll && !pty_.ended();
    const int ascent = glyphs_.ascent();
    for (int r = 0; r < rows; ++r) {
        int src = r - scroll_;
        if (src < -vt_.historySize()) continue;
        const Vt::Line& line = vt_.line(src);
        float y = oy + r * cellH_;
        int n = std::min(cols, (int)line.size());
        // Background runs, then glyphs; the cursor cell is drawn inverted.
        int runStart = 0;
        Color runColour = kBg;
        bool runSet = false;
        auto flushRun = [&](int end) {
            if (runSet) gfx.fill({ox + runStart * cellW_, y, (float)(end - runStart) * cellW_, (float)cellH_}, runColour);
        };
        for (int c = 0; c <= n; ++c) {
            bool set = false;
            Color bg = kBg;
            if (c < n) {
                const Vt::Cell& cell = line[c];
                bool inverse = cell.attr & Vt::kInverse;
                bool isCursor = cursor && src == vt_.cursorY() && c == vt_.cursorX();
                if (isCursor) {
                    bg = accent();
                    set = true;
                } else if (inverse || cell.bg != Vt::kDefault) {
                    bg = inverse ? colour(cell.fg, true) : colour(cell.bg, false);
                    set = true;
                }
            }
            bool same = set == runSet && (!set || (bg.r == runColour.r && bg.g == runColour.g && bg.b == runColour.b));
            if (!same || c == n) {
                flushRun(c);
                runStart = c;
                runColour = bg;
                runSet = set;
            }
        }
        for (int c = 0; c < n; ++c) {
            const Vt::Cell& cell = line[c];
            if (cell.cp == ' ' || (cell.attr & Vt::kHidden)) continue;
            bool isCursor = cursor && src == vt_.cursorY() && c == vt_.cursorX();
            Color fg = (cell.attr & Vt::kInverse) ? colour(cell.bg, false) : colour(cell.fg, true);
            if (isCursor) fg = kBg;
            if (cell.attr & Vt::kDim) fg = mix(fg, kBg, 0.4f);
            int style = (cell.attr & Vt::kBold ? Glyphs::Bold : 0) | (cell.attr & Vt::kItalic ? Glyphs::Italic : 0);
            glyphs_.add(glyphs_.glyph(style, cell.cp), ox + c * cellW_, y + ascent, fg);
            if (cell.attr & Vt::kUnderline) gfx.fill({ox + c * cellW_, y + ascent + 2, (float)cellW_, 1}, fg);
        }
    }
    glyphs_.flush(gfx.renderer);
    if (scroll_ > 0) {
        std::string s = strprintf("%d / %d", scroll_, vt_.historySize());
        float w = gfx.measure(Font::SmallBold, s) + 20;
        SDL_FRect r{gfx.W - w - 8, 8, w, 24};
        gfx.round(r, 12, accent(0.9f));
        gfx.text(Font::SmallBold, s, r.x + w / 2, r.y + (24 - gfx.lineHeight(Font::SmallBold)) / 2, kBg, 0, Align::Center);
    }
}

void Term::drawStrip(const SDL_FRect& r) {
    gfx.fill(r, rgb(0x12151c));
    if (suggestionsDirty_) {
        suggestionsDirty_ = false;
        suggestions_.clear();
        if (shellForeground_ && lineKnown_ && !pty_.ended()) suggestions_ = completer_.suggest(line_, pty_.cwd());
        suggestion_ = std::clamp(suggestion_, 0, std::max(0, (int)suggestions_.size() - 1));
    }
    float th = gfx.lineHeight(Font::Small);
    if (suggestions_.empty()) {
        std::string msg = !command_.empty() ? "Running " + command_
                          : !shellForeground_ ? "A program is running: SELECT for keys mode"
                          : !lineKnown_ ? "Suggestions return after Enter"
                                        : "Y inserts a suggestion, L1/R1 choose";
        gfx.text(Font::Small, msg, r.x + 10, r.y + (r.h - th) / 2, theme::faint, r.w - 20);
        return;
    }
    // Chips scroll so the chosen one stays visible.
    std::vector<float> widths;
    for (const auto& s : suggestions_)
        widths.push_back(std::min(260.f, gfx.measure(Font::Small, s.label) + (s.history ? 34 : 18)));
    float before = 0;
    for (int i = 0; i < suggestion_; ++i) before += widths[i] + 6;
    float shift = std::max(0.f, before + widths[suggestion_] + 12 - r.w);
    float x = r.x + 6 - shift;
    gfx.pushClip(r);
    for (size_t i = 0; i < suggestions_.size() && x < r.x + r.w; ++i) {
        float w = widths[i];
        if (x + w > r.x) {
            bool sel = (int)i == suggestion_;
            SDL_FRect chip{x, r.y + 3, w, r.h - 6};
            gfx.round(chip, chip.h / 2, sel ? accent() : theme::panelHi);
            Color fg = sel ? kBg : theme::text;
            float tx = chip.x + 9;
            if (suggestions_[i].history) {
                gfx.icon("history", tx - 2, chip.y + (chip.h - 16) / 2, 16, fg);
                tx += 16;
            }
            gfx.text(Font::Small, suggestions_[i].label, tx, chip.y + (chip.h - th) / 2, fg, chip.x + w - 9 - tx);
        }
        x += w + 6;
    }
    gfx.popClip();
}

void Term::drawBar(const std::vector<std::pair<std::string, std::string>>& hints, const std::string& right) {
    float y = gfx.H - kBarH;
    gfx.fill({0, y, gfx.W, kBarH}, rgb(0x12151c));
    float x = 8, bh = 16;
    float ty = y + (kBarH - gfx.lineHeight(Font::Tiny)) / 2;
    float rw = right.empty() ? 0 : gfx.measure(Font::Tiny, right) + 16;
    for (const auto& [key, label] : hints) {
        float need = gfx.buttonWidth(key, bh) + 4 + gfx.measure(Font::Tiny, label) + 10;
        if (x + need > gfx.W - rw) break;
        x += gfx.button(key, x, y + (kBarH - bh) / 2, bh, rgb(0xe8ebf0, 0.9f), kBg) + 4;
        x += gfx.text(Font::Tiny, label, x, ty, theme::dim) + 10;
    }
    if (!right.empty()) gfx.text(Font::Tiny, right, gfx.W - 8, ty, theme::faint, 0, Align::Right);
}

void Term::drawMenu() {
    float x = gfx.W - kPanelW;
    gfx.fill({0, 0, x, gfx.H}, rgb(0, 0.35f));
    gfx.fill({x, 0, kPanelW, gfx.H}, theme::panel);
    gfx.fill({x, 0, 1, gfx.H}, theme::line);
    gfx.text(Font::Bold, "Terminal", x + 20, 16, theme::text);
    menu_.draw({x + 6, 50, kPanelW - 12, gfx.H - 50 - 10});
}

void Term::drawHelp() {
    gfx.fill({0, 0, gfx.W, gfx.H}, rgb(0x0b0d12, 0.94f));
    gfx.text(Font::Bold, "Controls", 24, 16, theme::text);
    float y = 50, lh = gfx.lineHeight(Font::Mono) + 2;
    for (const char* l : kHelp) {
        gfx.text(Font::Mono, l, 24, y, l[0] == ' ' ? theme::text : theme::dim);
        y += lh;
    }
}

void Term::draw() {
    gfx.beginFrame();
    gfx.fill({0, 0, gfx.W, gfx.H}, kBg);
    drawGrid();
    Mode m = mode_ == Mode::Menu || mode_ == Mode::Help ? under_ : mode_;
    std::string title = vt_.title().empty() ? (command_.empty() ? std::string("sh") : command_) : vt_.title();
    switch (m) {
        case Mode::Type: {
            float kh = keyboard_.height();
            drawStrip({0, gfx.H - kh - kStripH, gfx.W, kStripH});
            keyboard_.draw({0, gfx.H - kh, gfx.W, kh});
            break;
        }
        case Mode::Keys:
            drawBar({{"SELECT", "Keyboard"}, {"START", "Menu"}, {"A", "Enter"}, {"B", "Esc"}, {"Y", "Tab"}, {"L2", "Ctrl-C"},
                     {"R2", "q"}, {"L1", "PgUp"}, {"R1", "PgDn"}, {"X", "Space"}},
                    "");
            break;
        case Mode::Scroll:
            drawBar({{"\xE2\x86\x95", "Line"}, {"L1", "Page"}, {"L2", "Oldest"}, {"R2", "Newest"}, {"B", "Done"}},
                    strprintf("scrollback %d / %d", scroll_, vt_.historySize()));
            break;
        case Mode::Exited:
            drawBar({}, strprintf("exited with code %d, press any button", exitCode_));
            break;
        default: break;
    }
    if (mode_ == Mode::Menu) drawMenu();
    if (mode_ == Mode::Help) drawHelp();
    if (!shot_.empty()) {
        if (SDL_Surface* surf = SDL_RenderReadPixels(gfx.renderer, nullptr)) {
            SDL_SaveBMP(surf, shot_.c_str());
            SDL_DestroySurface(surf);
        }
        shot_.clear();
    }
    gfx.endFrame();
}

// Development aid, as in kennel: KENNEL_AUTOMATE="type:ls -l,start,wait:500,shot:/tmp/a.bmp,quit".
// type:TEXT sends TEXT as typed on the keyboard.
void Term::automate(std::vector<KeyEvent>& keys) {
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
    else if (tok.rfind("type:", 0) == 0) type(tok.substr(5), false);
    else if (tok == "quit") running_ = false;
    else
        for (const auto& [n, k] : names)
            if (tok == n) keys.push_back({k, false});
    dirty_ = true;
}

// ----------------------------------------------------------------- main ---

int Term::run(int argc, char** argv) {
    std::string cwd;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--cwd" && i + 1 < argc) cwd = argv[++i];
        else if (a == "-e" && i + 1 < argc) command_ = argv[++i];
        else {
            fprintf(stderr, "usage: kennel-term [--cwd DIR] [-e COMMAND]\n");
            return 2;
        }
    }
    paths.init(argv[0]);
    logToFile(paths.runtime + "/kennel-term.log");
    platform = detectPlatform();
    settings.load(paths.config + "/kennel.ini");
    loadSettings();
    anim::setScale(0);
    signal(SIGPIPE, SIG_IGN);
    shellForeground_ = command_.empty();

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!gfx.init(platform.width, platform.height, platform.fullscreen)) return 1;
    if (!openFont()) {
        logf("no terminal font");
        return 1;
    }
    ::input.repeatAll = true;
    completer_.load(paths.data + "/term_history");
    layout();
    std::string error;
    if (!pty_.start(cwd, command_, vt_.cols(), vt_.rows(), error)) {
        logf("%s", error.c_str());
        vt_.feed("kennel-term: " + error + "\r\n");
        exitCode_ = 1;
        setMode(Mode::Exited);
    }
    SDL_StartTextInput(gfx.window);

    if (const char* script = getenv("KENNEL_AUTOMATE")) script_ = split(script, ',');
    bool gamepadStarted = false;
    std::vector<KeyEvent> keys;
    while (running_) {
        anim::tick();
        int timeout = -1;
        int repeat = ::input.timeoutMs();
        if (repeat >= 0) timeout = repeat;
        if (std::abs(stickY_) > kStickDead) timeout = timeout < 0 ? 30 : std::min(timeout, 30);
        if (!script_.empty()) timeout = timeout < 0 ? 50 : std::min(timeout, 50);
        if (dirty_ || !gamepadStarted) timeout = 0;
        SDL_Event ev;
        bool got = waitEvent(&ev, timeout, kInputPollMs);
        keys.clear();
        while (got) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: running_ = false; break;
                case SDL_EVENT_WINDOW_EXPOSED: dirty_ = true; break;
                case SDL_EVENT_TEXT_INPUT:
                    if (mode_ == Mode::Type || mode_ == Mode::Keys) type(ev.text.text, false);
                    break;
                case SDL_EVENT_KEY_DOWN: keyboard(ev.key); break;
                case SDL_EVENT_KEY_UP: break;
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                    if (ev.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) stickY_ = ev.gaxis.value;
                    else ::input.handle(ev, keys);
                    break;
                default: ::input.handle(ev, keys);
            }
            got = SDL_PollEvent(&ev);
        }
        anim::tick();
        output();
        ::input.tick(keys);
        automate(keys);
        for (const auto& k : keys) input(k);
        if (std::abs(stickY_) > kStickDead && anim::now() >= nextStick_) {
            scrollBy(stickY_ < 0 ? 1 : -1);
            nextStick_ = anim::now() + (std::abs(stickY_) > kStickFast ? 0.03 : 0.09);
        }

        if (dirty_ && running_) {
            dirty_ = false;
            draw();
            if (!gamepadStarted) {
                gamepadStarted = true;
                if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) logf("gamepad init: %s", SDL_GetError());
            }
        }
    }

    SDL_StopTextInput(gfx.window);
    pty_.hangup();
    glyphs_.close();
    gfx.shutdown();
    SDL_Quit();
    return command_.empty() ? 0 : exitCode_;
}
