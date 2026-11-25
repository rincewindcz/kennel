#include "calc.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "anim.h"
#include "gfx.h"
#include "ini.h"
#include "menu.h"
#include "paths.h"
#include "platform.h"
#include "settings.h"
#include "util.h"

namespace {

constexpr float kMargin = 12, kGap = 6, kBlockGap = 8, kDisplayH = 124, kBarH = 24;
constexpr int kInputPollMs = 10;  // waitEvent() interval
const Color kDark = rgb(0x0b0d12);

using calc::Kind;

bool isOp(const calc::Token& t, int id) { return t.kind == Kind::Op && t.id == id; }

// Right-aligned text that keeps its end visible when it is wider than the space left of `right`.
void rightText(Font f, const std::string& s, float left, float right, float y, Color c) {
    float w = gfx.measure(f, s);
    if (w <= right - left) {
        gfx.text(f, s, right, y, c, 0, Align::Right);
        return;
    }
    gfx.pushClip({left, y, right - left, gfx.lineHeight(f)});
    gfx.text(f, s, right - w, y, c);
    gfx.popClip();
}

}  // namespace

// --------------------------------------------------------------- setup ---

void Calc::buildGrids() {
    using namespace calc;
    auto key = [](std::string label, Token t, Style s = Style::Function) {
        Pad p;
        p.label = std::move(label);
        p.token = std::move(t);
        p.style = s;
        return p;
    };
    auto alt = [&](std::string label, Token t, std::string label2, Token t2) {
        Pad p = key(std::move(label), std::move(t));
        p.label2 = std::move(label2);
        p.token2 = std::move(t2);
        return p;
    };
    auto act = [](std::string label, Act a, Style s = Style::Function, std::string icon = {}) {
        Pad p;
        p.label = std::move(label);
        p.act = a;
        p.style = s;
        p.icon = std::move(icon);
        return p;
    };
    auto num = [&](char c) { return key(std::string(1, c), digit(c), Style::Digit); };
    const char* inv = "\xE2\x81\xBB\xC2\xB9";

    basic_ = {
        {act("MC", Act::MemClear), act("MR", Act::MemRecall), act("M+", Act::MemAdd), act("M\xE2\x88\x92", Act::MemSub)},
        {act("C", Act::Clear), act("", Act::Back, Style::Function, "delete"), key("%", postfix(Percent)), key("\xC3\xB7", op(Div))},
        {num('7'), num('8'), num('9'), key("\xC3\x97", op(Mul))},
        {num('4'), num('5'), num('6'), key("\xE2\x88\x92", op(Sub))},
        {num('1'), num('2'), num('3'), key("+", op(Add))},
        {act("\xC2\xB1", Act::Negate, Style::Digit), num('0'), key(".", point(), Style::Digit), act("=", Act::Equals, Style::Equals)},
    };
    Grid left = {
        {act("2nd", Act::Second), act("DEG", Act::Angle), key("(", lparen()), key(")", rparen()), act("Ans", Act::Ans)},
        {alt("sin", func(Sin), std::string("sin") + inv, func(Asin)), alt("cos", func(Cos), std::string("cos") + inv, func(Acos)),
         alt("tan", func(Tan), std::string("tan") + inv, func(Atan)), alt("ln", func(Ln), "e\xCB\xA3", func(Exp)),
         alt("log", func(Log), "10\xCB\xA3", func(Exp10))},
        {alt("sinh", func(Sinh), std::string("sinh") + inv, func(Asinh)), alt("cosh", func(Cosh), std::string("cosh") + inv, func(Acosh)),
         alt("tanh", func(Tanh), std::string("tanh") + inv, func(Atanh)), alt("log\xE2\x82\x82", func(Log2), "2\xCB\xA3", func(Exp2)),
         alt("\xE2\x88\x9Ax", func(Sqrt), "\xC2\xB3\xE2\x88\x9Ax", func(Cbrt))},
        {key("x\xC2\xB2", postfix(Square)), key("x\xC2\xB3", postfix(Cube)), alt("x\xCA\xB8", op(Pow), "\xCA\xB8\xE2\x88\x9Ax", op(Root)),
         key("1/x", postfix(Inverse)), key("|x|", func(Abs))},
        {key("n!", postfix(Factorial)), key("nCr", op(Choose)), key("nPr", op(Permute)), key("mod", op(Mod)), act("rand", Act::Rand)},
        {key("\xCF\x80", constant(Pi)), key("e", constant(Euler)), key("EE", exponent()), key("floor", func(Floor)), key("ceil", func(Ceil))},
    };
    sci_ = left;
    for (size_t r = 0; r < sci_.size(); ++r) sci_[r].insert(sci_[r].end(), basic_[r].begin(), basic_[r].end());
}

void Calc::loadSettings() {
    settingsPath_ = paths.config + "/calc.ini";
    IniFile ini;
    if (!loadIni(settingsPath_, ini)) return;
    scientific_ = ini.get("calc", "mode") == "scientific";
    degrees_ = ini.get("calc", "angle", "deg") != "rad";
    memory_ = strtod(ini.get("calc", "memory", "0").c_str(), nullptr);
    ans_ = strtod(ini.get("calc", "ans", "0").c_str(), nullptr);
    if (!std::isfinite(memory_)) memory_ = 0;
    if (!std::isfinite(ans_)) ans_ = 0;
}

void Calc::saveSettings() {
    writeFileAtomic(settingsPath_, strprintf("[calc]\nmode=%s\nangle=%s\nmemory=%.17g\nans=%.17g\n", scientific_ ? "scientific" : "basic",
                                             degrees_ ? "deg" : "rad", memory_, ans_));
}

// -------------------------------------------------------------- editing ---

void Calc::press(const Pad& p) {
    bool second = second_ && !p.label2.empty();
    if (p.act != Act::Second) second_ = false;
    switch (p.act) {
        case Act::Token: insert(second ? p.token2 : p.token); break;
        case Act::Clear: clear(); break;
        case Act::Back: back(); break;
        case Act::Equals: equals(); break;
        case Act::Negate: negate(); break;
        case Act::MemClear:
            memory_ = 0;
            saveSettings();
            break;
        case Act::MemRecall: insert(calc::number(memory_)); break;
        case Act::MemAdd:
        case Act::MemSub: {
            double v;
            if (!currentValue(v)) break;
            memory_ += p.act == Act::MemAdd ? v : -v;
            if (memory_ == 0 || !std::isfinite(memory_)) memory_ = 0;
            saveSettings();
            break;
        }
        case Act::Second: second_ = !second_; break;
        case Act::Angle:
            degrees_ = !degrees_;
            refresh();
            saveSettings();
            break;
        case Act::Ans: insert(calc::number(ans_, "Ans")); break;
        case Act::Rand: insert(calc::number(std::uniform_real_distribution<double>(0, 1)(rng_))); break;
    }
    dirty_ = true;
}

void Calc::insert(calc::Token t) {
    error_.clear();
    bool binary = t.kind == Kind::Op;
    bool continues = binary || t.kind == Kind::Postfix;
    if (evaluated_) {
        tokens_.clear();
        if (continues) tokens_.push_back(calc::number(ans_));
        evaluated_ = false;
    } else if (tokens_.empty() && continues && !isOp(t, calc::Sub)) {
        tokens_.push_back(calc::number(ans_));
    }

    auto lastIs = [&](Kind k) { return !tokens_.empty() && tokens_.back().kind == k; };
    // digits of the number being entered, and whether it already has a point or an exponent
    size_t s = tokens_.size();
    while (s > 0 && tokens_[s - 1].kind == Kind::Digit) --s;
    bool hasPoint = false, hasExp = false;
    for (size_t i = s; i-- > 0;) {
        Kind k = tokens_[i].kind;
        if (k == Kind::Point) hasPoint = true;
        else if (k == Kind::Exp) hasExp = true;
        else if (k != Kind::Digit) break;
    }

    switch (t.kind) {
        case Kind::Point:
            if (hasPoint || hasExp) return;
            if (!lastIs(Kind::Digit)) tokens_.push_back(calc::digit('0'));
            break;
        case Kind::Exp:
            if (hasExp) return;
            if (!lastIs(Kind::Digit) && !lastIs(Kind::Point)) tokens_.push_back(calc::digit('1'));
            break;
        case Kind::Op:
            // a second operator replaces the first, except a minus that starts a negative number
            if (lastIs(Kind::Op) && !(tokens_.size() > 1 && tokens_[tokens_.size() - 2].kind == Kind::Exp)) {
                const calc::Token& last = tokens_.back();
                if (!isOp(t, calc::Sub) || isOp(last, calc::Add) || isOp(last, calc::Sub)) tokens_.pop_back();
            }
            break;
        default: break;
    }
    tokens_.push_back(std::move(t));
    refresh();
}

void Calc::negate() {
    error_.clear();
    if (evaluated_) {
        ans_ = -ans_;
        if (ans_ == 0) ans_ = 0;
        dirty_ = true;
        return;
    }
    size_t n = tokens_.size(), s = n;
    while (s > 0) {
        Kind k = tokens_[s - 1].kind;
        bool expSign = k == Kind::Op && s >= 2 && tokens_[s - 2].kind == Kind::Exp;
        if (k == Kind::Digit || k == Kind::Point || k == Kind::Exp || expSign) --s;
        else break;
    }
    if (s == n) {
        if (n == 0 || (tokens_.back().kind != Kind::Number && tokens_.back().kind != Kind::Const)) {
            tokens_.push_back(calc::op(calc::Sub));
            refresh();
            return;
        }
        s = n - 1;
    }
    bool unary = s > 0 && isOp(tokens_[s - 1], calc::Sub) &&
                 (s == 1 || tokens_[s - 2].kind == Kind::Op || tokens_[s - 2].kind == Kind::LParen || tokens_[s - 2].kind == Kind::Func);
    if (unary) tokens_.erase(tokens_.begin() + (long)(s - 1));
    else tokens_.insert(tokens_.begin() + (long)s, calc::op(calc::Sub));
    refresh();
}

void Calc::back() {
    error_.clear();
    if (evaluated_) {
        evaluated_ = false;
        tokens_.clear();
    } else if (tokens_.empty()) {
        running_ = false;
        return;
    } else {
        tokens_.pop_back();
    }
    refresh();
}

void Calc::clear() {
    if (tokens_.empty() && !evaluated_) {
        ans_ = 0;
        last_.clear();
        history_.clear();
    }
    tokens_.clear();
    evaluated_ = false;
    error_.clear();
    refresh();
}

void Calc::equals() {
    if (evaluated_ || tokens_.empty()) return;
    calc::Result r = calc::evaluate(tokens_, degrees_);
    if (!r.ok) {
        error_ = r.error.empty() ? "Syntax error" : r.error;
        dirty_ = true;
        return;
    }
    history_ = calc::display(tokens_) + " =";
    ans_ = r.value;
    last_ = history_ + " " + calc::format(ans_);
    evaluated_ = true;
    tokens_.clear();
    refresh();
}

void Calc::refresh() {
    preview_ = tokens_.empty() ? calc::Result{} : calc::evaluate(tokens_, degrees_);
    dirty_ = true;
}

bool Calc::currentValue(double& out) const {
    if (evaluated_) out = ans_;
    else if (preview_.ok) out = preview_.value;
    else return false;
    return true;
}

void Calc::setScientific(bool on) {
    if (on == scientific_) return;
    scientific_ = on;
    int extra = (int)(sci_[0].size() - basic_[0].size());
    col_ = on ? col_ + extra : std::max(0, col_ - extra);
    second_ = false;
    saveSettings();
    dirty_ = true;
}

void Calc::move(int dr, int dc, bool repeat) {
    const Grid& g = grid();
    int rows = (int)g.size(), cols = (int)g[0].size();
    auto step = [&](int v, int d, int n) { return repeat ? std::clamp(v + d, 0, n - 1) : (v + d + n) % n; };
    if (dr) row_ = step(row_, dr, rows);
    if (dc) col_ = step(col_, dc, cols);
    dirty_ = true;
}

// --------------------------------------------------------------- input ---

void Calc::input(const KeyEvent& ev) {
    switch (ev.key) {
        case Key::Up: move(-1, 0, ev.repeat); break;
        case Key::Down: move(1, 0, ev.repeat); break;
        case Key::Left: move(0, -1, ev.repeat); break;
        case Key::Right: move(0, 1, ev.repeat); break;
        case Key::Confirm: press(grid()[row_][col_]); break;
        case Key::Back: back(); break;
        case Key::X: clear(); break;
        case Key::Y: negate(); break;
        case Key::Start: equals(); break;
        case Key::L1:
            if (!ev.repeat) insert(calc::lparen());
            break;
        case Key::R1:
            if (!ev.repeat) insert(calc::rparen());
            break;
        case Key::L2:
            if (scientific_) press(sci_[0][0]);
            break;
        case Key::R2:
            if (scientific_) press(sci_[0][1]);
            break;
        case Key::Select: setScientific(!scientific_); break;
        default: break;
    }
}

void Calc::keyboard(const SDL_KeyboardEvent& k) {
    switch (k.key) {
        case SDLK_UP: move(-1, 0, k.repeat); break;
        case SDLK_DOWN: move(1, 0, k.repeat); break;
        case SDLK_LEFT: move(0, -1, k.repeat); break;
        case SDLK_RIGHT: move(0, 1, k.repeat); break;
        case SDLK_SPACE: press(grid()[row_][col_]); break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: equals(); break;
        case SDLK_BACKSPACE: back(); break;
        case SDLK_DELETE: clear(); break;
        case SDLK_TAB: setScientific(!scientific_); break;
        case SDLK_ESCAPE: running_ = false; break;
        default: break;
    }
}

void Calc::text(const char* s) {
    for (const char* p = s; *p; ++p) {
        if (*p == ' ') continue;
        if (*p == '=') {
            equals();
            continue;
        }
        std::vector<calc::Token> t;
        if (calc::parse(std::string(1, *p), t))
            for (auto& tok : t) insert(std::move(tok));
    }
}

// Development aid, as in kennel: KENNEL_AUTOMATE="type:2+3*4,start,wait:300,shot:/tmp/c.bmp,quit".
// type:TEXT enters TEXT as on a keyboard.
void Calc::automate(std::vector<KeyEvent>& keys) {
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
    else if (tok.rfind("type:", 0) == 0) {
        std::vector<calc::Token> t;
        if (calc::parse(tok.substr(5), t))
            for (auto& x : t) insert(std::move(x));
    } else if (tok == "quit") running_ = false;
    else
        for (const auto& [n, k] : names)
            if (tok == n) keys.push_back({k, false});
    dirty_ = true;
}

// ---------------------------------------------------------------- draw ---

void Calc::drawDisplay(const SDL_FRect& r) {
    gfx.round(r, 14, theme::panel);
    float x = r.x + 16, right = r.x + r.w - 16;
    float ty = r.y + 12;
    auto tag = [&](const std::string& s, Color c) { x += gfx.text(Font::Tiny, s, x, ty, c) + 12; };
    tag(scientific_ ? "SCIENTIFIC" : "BASIC", theme::faint);
    if (scientific_) tag(degrees_ ? "DEG" : "RAD", theme::dim);
    if (second_) tag("2nd", accent());
    if (memory_ != 0) tag("M", accent());

    const std::string& top = evaluated_ ? history_ : last_;
    if (!top.empty()) rightText(Font::Small, top, x + 8, right, r.y + 10, theme::dim);

    std::string main = evaluated_ ? calc::format(ans_) : tokens_.empty() ? "0" : calc::display(tokens_);
    Font f = gfx.measure(Font::Huge, main) <= right - r.x - 16 ? Font::Huge : Font::Title;
    float my = r.y + 36 + (gfx.lineHeight(Font::Huge) - gfx.lineHeight(f)) / 2;
    rightText(f, main, r.x + 16, right, my, theme::text);

    float py = r.y + r.h - 14 - gfx.lineHeight(Font::Body);
    if (!error_.empty()) rightText(Font::Body, error_, r.x + 16, right, py, theme::warn);
    else if (!evaluated_ && preview_.ok && calc::hasOperation(tokens_))
        rightText(Font::Body, "= " + calc::format(preview_.value), r.x + 16, right, py, theme::dim);
    else if (!evaluated_ && !preview_.ok && !preview_.syntax && !preview_.error.empty())
        rightText(Font::Body, preview_.error, r.x + 16, right, py, fade(theme::warn, 0.7f));
}

void Calc::drawGrid(const SDL_FRect& r) {
    const Grid& g = grid();
    int rows = (int)g.size(), cols = (int)g[0].size();
    int split = scientific_ ? cols - (int)basic_[0].size() : 0;
    float block = split ? kBlockGap : 0;
    float kw = (r.w - (cols - 1) * kGap - block) / cols;
    float kh = (r.h - (rows - 1) * kGap) / rows;
    for (int ri = 0; ri < rows; ++ri) {
        for (int ci = 0; ci < cols; ++ci) {
            const Pad& p = g[ri][ci];
            SDL_FRect k{r.x + ci * (kw + kGap) + (split && ci >= split ? block : 0), r.y + ri * (kh + kGap), kw, kh};
            bool focus = ri == row_ && ci == col_;
            bool latched = p.act == Act::Second && second_;
            Color bg = focus     ? accent()
                       : latched ? accent(0.35f)
                       : p.style == Style::Equals ? accent(0.3f)
                       : p.style == Style::Digit  ? theme::panel
                                                  : theme::panelHi;
            gfx.round(k, 9, bg);
            Color fg = focus ? kDark : theme::text;
            if (!p.icon.empty()) {
                gfx.icon(p.icon, k.x + (k.w - 20) / 2, k.y + (k.h - 20) / 2, 20, fg);
                continue;
            }
            std::string label = p.act == Act::Angle ? (degrees_ ? "DEG" : "RAD") : second_ && !p.label2.empty() ? p.label2 : p.label;
            Font f = p.style == Style::Function ? (scientific_ ? Font::SmallBold : Font::Bold) : scientific_ ? Font::Bold : Font::Title;
            if (gfx.measure(f, label) > kw - 8) f = Font::Tiny;
            gfx.text(f, label, k.x + k.w / 2, k.y + (k.h - gfx.lineHeight(f)) / 2, fg, 0, Align::Center);
        }
    }
}

void Calc::drawBar() {
    float y = gfx.H - kBarH;
    gfx.fill({0, y, gfx.W, kBarH}, rgb(0x12151c));
    std::vector<std::pair<std::string, std::string>> hints = {
        {::input.label(Key::Confirm), "Press"},
        {::input.label(Key::Back), tokens_.empty() && !evaluated_ ? "Exit" : "Delete"},
        {::input.label(Key::X), "Clear"},
        {::input.label(Key::Y), "\xC2\xB1"},
        {"START", "="},
        {"L1", "("},
        {"R1", ")"},
        {"SELECT", scientific_ ? "Basic" : "Scientific"},
    };
    if (scientific_) {
        hints.push_back({"L2", "2nd"});
        hints.push_back({"R2", degrees_ ? "RAD" : "DEG"});
    }
    float x = 8, bh = 16;
    float ty = y + (kBarH - gfx.lineHeight(Font::Tiny)) / 2;
    for (const auto& [key, label] : hints) {
        float need = gfx.buttonWidth(key, bh) + 4 + gfx.measure(Font::Tiny, label) + 10;
        if (x + need > gfx.W) break;
        x += gfx.button(key, x, y + (kBarH - bh) / 2, bh, rgb(0xe8ebf0, 0.9f), kDark) + 4;
        x += gfx.text(Font::Tiny, label, x, ty, theme::dim) + 10;
    }
}

void Calc::draw() {
    gfx.beginFrame();
    gfx.gradient({0, 0, gfx.W, gfx.H}, theme::bgTop, theme::bgBottom);
    SDL_FRect display{kMargin, kMargin, gfx.W - 2 * kMargin, kDisplayH};
    drawDisplay(display);
    float top = display.y + display.h + 10;
    drawGrid({kMargin, top, gfx.W - 2 * kMargin, gfx.H - kBarH - 8 - top});
    drawBar();
    if (!shot_.empty()) {
        if (SDL_Surface* surf = SDL_RenderReadPixels(gfx.renderer, nullptr)) {
            SDL_SaveBMP(surf, shot_.c_str());
            SDL_DestroySurface(surf);
        }
        shot_.clear();
    }
    gfx.endFrame();
}

// ----------------------------------------------------------------- main ---

int Calc::run(int, char** argv) {
    uint64_t t0 = SDL_GetTicksNS();
    paths.init(argv[0]);
    logToFile(paths.runtime + "/kennel-calc.log");
    platform = detectPlatform();
    settings.load(paths.config + "/kennel.ini");
    loadSettings();
    buildGrids();
    if (scientific_) col_ = (int)(sci_[0].size() - basic_[0].size());
    anim::setScale(0);

    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        logf("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (!gfx.init(platform.width, platform.height, platform.fullscreen)) return 1;
    SDL_StartTextInput(gfx.window);

    if (const char* script = getenv("KENNEL_AUTOMATE")) script_ = split(script, ',');
    bool gamepadStarted = false;
    std::vector<KeyEvent> keys;
    while (running_) {
        anim::tick();
        int timeout = ::input.timeoutMs();
        if (!script_.empty()) timeout = timeout < 0 ? 50 : std::min(timeout, 50);
        if (dirty_ || !gamepadStarted) timeout = 0;
        SDL_Event ev;
        bool got = waitEvent(&ev, timeout, kInputPollMs);
        keys.clear();
        while (got) {
            switch (ev.type) {
                case SDL_EVENT_QUIT: running_ = false; break;
                case SDL_EVENT_WINDOW_EXPOSED: dirty_ = true; break;
                case SDL_EVENT_TEXT_INPUT: text(ev.text.text); break;
                case SDL_EVENT_KEY_DOWN: keyboard(ev.key); break;
                case SDL_EVENT_KEY_UP: break;
                default: ::input.handle(ev, keys);
            }
            got = SDL_PollEvent(&ev);
        }
        anim::tick();
        ::input.tick(keys);
        automate(keys);
        for (const auto& k : keys) input(k);

        if (dirty_ && running_) {
            dirty_ = false;
            draw();
            if (!gamepadStarted) {
                gamepadStarted = true;
                logf("first frame at %.0f ms (draw %.0f ms)", (SDL_GetTicksNS() - t0) / 1e6, gfx.frameMs);
                if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) logf("gamepad init: %s", SDL_GetError());
                // KMSDRM does not scan out the first presented frame; an idle screen needs a second one.
                dirty_ = true;
            }
        }
    }

    saveSettings();
    SDL_StopTextInput(gfx.window);
    gfx.shutdown();
    SDL_Quit();
    return 0;
}
