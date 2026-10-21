#include "vt.h"

#include <cstdio>

namespace {

constexpr size_t kHistory = 2000;

// DEC special graphics for 0x60..0x7e.
const uint16_t kGraphics[] = {0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1, 0x2424, 0x240b, 0x2518,
                              0x2510, 0x250c, 0x2514, 0x253c, 0x23ba, 0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524,
                              0x2534, 0x252c, 0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7};

bool zeroWidth(uint32_t cp) {
    return (cp >= 0x300 && cp <= 0x36f) || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0xfeff;
}

}  // namespace

Vt::Vt() { resize(80, 24); }

Vt::Cell Vt::blank() const {
    Cell c;
    c.bg = pen_.bg;
    return c;
}

const Vt::Line& Vt::line(int row) const {
    if (row < 0) return history_[history_.size() + row];
    return (alt_ ? altLines_ : lines_)[row];
}

std::string Vt::takeReply() {
    std::string r;
    r.swap(reply_);
    return r;
}

void Vt::reset() {
    pen_ = Cell{};
    for (auto* screen : {&lines_, &altLines_})
        for (auto& l : *screen) l.assign(cols_, Cell{});
    alt_ = false;
    x_ = y_ = 0;
    wrapPending_ = false;
    top_ = 0;
    bottom_ = rows_ - 1;
    autowrap_ = true;
    insert_ = originMode_ = false;
    cursorVisible_ = true;
    appCursor_ = bracketedPaste_ = false;
    g0Graphics_ = g1Graphics_ = shiftOut_ = false;
    saved_ = savedAlt_ = Saved{};
}

void Vt::resize(int cols, int rows) {
    cols = std::max(2, cols);
    rows = std::max(2, rows);
    if (cols == cols_ && rows == rows_ && !lines_.empty()) return;
    // Keep the cursor line on screen: lines above it move into the scrollback.
    int shift = std::max(0, y_ - (rows - 1));
    for (auto* screen : {&lines_, &altLines_}) {
        screen->resize(std::max<size_t>(screen->size(), (size_t)rows_), Line(cols_, Cell{}));
        for (int i = 0; i < shift && !screen->empty(); ++i) {
            if (screen == &lines_) {
                history_.push_back(std::move(screen->front()));
                if (history_.size() > kHistory) history_.pop_front();
            }
            screen->erase(screen->begin());
        }
        screen->resize(rows, Line(cols, Cell{}));
        for (auto& l : *screen) l.resize(cols, Cell{});
    }
    cols_ = cols;
    rows_ = rows;
    y_ = std::clamp(y_ - shift, 0, rows - 1);
    x_ = std::clamp(x_, 0, cols - 1);
    saved_.y = std::clamp(saved_.y - shift, 0, rows - 1);
    saved_.x = std::clamp(saved_.x, 0, cols - 1);
    savedAlt_.y = std::clamp(savedAlt_.y, 0, rows - 1);
    savedAlt_.x = std::clamp(savedAlt_.x, 0, cols - 1);
    top_ = 0;
    bottom_ = rows - 1;
    wrapPending_ = false;
}

// ------------------------------------------------------------- editing ---

void Vt::scrollUp(int top, int bottom, int n, bool keep) {
    auto& s = screen();
    n = std::min(n, bottom - top + 1);
    for (int i = 0; i < n; ++i) {
        if (keep && !alt_ && top == 0) {
            history_.push_back(s[top]);
            if (history_.size() > kHistory) history_.pop_front();
        }
        s.erase(s.begin() + top);
        s.insert(s.begin() + bottom, Line(cols_, blank()));
    }
}

void Vt::scrollDown(int top, int bottom, int n) {
    auto& s = screen();
    n = std::min(n, bottom - top + 1);
    for (int i = 0; i < n; ++i) {
        s.erase(s.begin() + bottom);
        s.insert(s.begin() + top, Line(cols_, blank()));
    }
}

void Vt::clearCells(int row, int from, int to) {
    auto& l = screen()[row];
    for (int x = std::max(0, from); x < std::min(to, cols_); ++x) l[x] = blank();
}

void Vt::lineFeed() {
    wrapPending_ = false;
    if (y_ == bottom_) scrollUp(top_, bottom_, 1, true);
    else if (y_ < rows_ - 1) ++y_;
}

void Vt::reverseIndex() {
    wrapPending_ = false;
    if (y_ == top_) scrollDown(top_, bottom_, 1);
    else if (y_ > 0) --y_;
}

void Vt::moveTo(int x, int y) {
    if (originMode_) {
        y += top_;
        y_ = std::clamp(y, top_, bottom_);
    } else {
        y_ = std::clamp(y, 0, rows_ - 1);
    }
    x_ = std::clamp(x, 0, cols_ - 1);
    wrapPending_ = false;
}

void Vt::put(uint32_t cp) {
    if (zeroWidth(cp)) return;
    bool graphics = shiftOut_ ? g1Graphics_ : g0Graphics_;
    if (graphics && cp >= 0x5f && cp <= 0x7e) cp = cp == 0x5f ? ' ' : kGraphics[cp - 0x60];
    if (wrapPending_ && autowrap_) {
        x_ = 0;
        lineFeed();
    }
    wrapPending_ = false;
    auto& l = screen()[y_];
    if (insert_) {
        l.insert(l.begin() + x_, blank());
        l.pop_back();
    }
    Cell c = pen_;
    c.cp = cp;
    l[x_] = c;
    lastChar_ = cp;
    if (x_ == cols_ - 1) wrapPending_ = autowrap_;
    else ++x_;
}

// -------------------------------------------------------------- parser ---

void Vt::feed(std::string_view data) {
    for (unsigned char b : data) {
        // C0 controls act in every state but the string states.
        if (state_ != State::Osc && state_ != State::Ignore && state_ != State::IgnoreEscape &&
            state_ != State::OscEscape && (b < 0x20 || b == 0x7f) && b != 0x1b) {
            if (b == 0x18 || b == 0x1a) {
                state_ = State::Ground;
                continue;
            }
            utf8Left_ = 0;
            switch (b) {
                case 0x08:
                    if (x_ > 0) x_ = std::min(x_, cols_ - 1) - 1;
                    wrapPending_ = false;
                    break;
                case 0x09:
                    x_ = std::min(cols_ - 1, (x_ / 8 + 1) * 8);
                    wrapPending_ = false;
                    break;
                case 0x0a:
                case 0x0b:
                case 0x0c: lineFeed(); break;
                case 0x0d:
                    x_ = 0;
                    wrapPending_ = false;
                    break;
                case 0x0e: shiftOut_ = true; break;
                case 0x0f: shiftOut_ = false; break;
                default: break;
            }
            continue;
        }

        switch (state_) {
            case State::Ground:
                if (b == 0x1b) {
                    utf8Left_ = 0;
                    state_ = State::Escape;
                } else if (utf8Left_ > 0) {
                    if ((b & 0xc0) == 0x80) {
                        utf8_ = utf8_ << 6 | (b & 0x3f);
                        if (--utf8Left_ == 0) put(utf8_);
                    } else {
                        utf8Left_ = 0;
                        put(0xfffd);
                        if (b < 0x80) put(b);
                    }
                } else if (b < 0x80) {
                    put(b);
                } else if ((b & 0xe0) == 0xc0) {
                    utf8_ = b & 0x1f;
                    utf8Left_ = 1;
                } else if ((b & 0xf0) == 0xe0) {
                    utf8_ = b & 0x0f;
                    utf8Left_ = 2;
                } else if ((b & 0xf8) == 0xf0) {
                    utf8_ = b & 0x07;
                    utf8Left_ = 3;
                } else {
                    put(0xfffd);
                }
                break;

            case State::Escape:
                switch (b) {
                    case '[':
                        params_.clear();
                        intermediates_.clear();
                        private_ = 0;
                        state_ = State::Csi;
                        break;
                    case ']':
                        osc_.clear();
                        state_ = State::Osc;
                        break;
                    case 'P':
                    case 'X':
                    case '^':
                    case '_': state_ = State::Ignore; break;
                    case '(':
                    case ')':
                    case '*':
                    case '+':
                    case '#':
                    case ' ':
                    case '%':
                        charsetTarget_ = (char)b;
                        state_ = State::Charset;
                        break;
                    case 0x1b: break;
                    default:
                        escape((char)b);
                        state_ = State::Ground;
                }
                break;

            case State::Charset:
                if (charsetTarget_ == '(') g0Graphics_ = b == '0';
                else if (charsetTarget_ == ')') g1Graphics_ = b == '0';
                state_ = State::Ground;
                break;

            case State::Csi:
                if (b == 0x1b) {
                    state_ = State::Escape;
                } else if (b >= '0' && b <= '9') {
                    if (params_.empty()) params_.push_back(-1);
                    int& p = params_.back();
                    p = std::min(99999, (p < 0 ? 0 : p) * 10 + (b - '0'));
                } else if (b == ';' || b == ':') {
                    if (params_.empty()) params_.push_back(-1);
                    params_.push_back(-1);
                } else if (b >= '<' && b <= '?') {
                    private_ = (char)b;
                } else if (b >= 0x20 && b <= 0x2f) {
                    intermediates_ += (char)b;
                } else if (b >= 0x40 && b <= 0x7e) {
                    csi((char)b);
                    state_ = State::Ground;
                }
                break;

            case State::Osc:
                if (b == 0x07) {
                    if (osc_.rfind("0;", 0) == 0 || osc_.rfind("2;", 0) == 0) title_ = osc_.substr(2);
                    state_ = State::Ground;
                } else if (b == 0x1b) {
                    state_ = State::OscEscape;
                } else if (osc_.size() < 512) {
                    osc_ += (char)b;
                }
                break;

            case State::OscEscape:
                if (osc_.rfind("0;", 0) == 0 || osc_.rfind("2;", 0) == 0) title_ = osc_.substr(2);
                state_ = State::Ground;
                break;

            case State::Ignore:
                if (b == 0x1b) state_ = State::IgnoreEscape;
                break;

            case State::IgnoreEscape: state_ = b == '\\' ? State::Ground : State::Ignore; break;
        }
    }
}

void Vt::escape(char c) {
    switch (c) {
        case '7':
            saved_ = {x_, y_, pen_, g0Graphics_};
            break;
        case '8':
            x_ = saved_.x;
            y_ = saved_.y;
            pen_ = saved_.pen;
            g0Graphics_ = saved_.g0Graphics;
            wrapPending_ = false;
            break;
        case 'D': lineFeed(); break;
        case 'E':
            x_ = 0;
            lineFeed();
            break;
        case 'M': reverseIndex(); break;
        case 'c': reset(); break;
        default: break;
    }
}

int Vt::param(size_t i, int fallback) const {
    return i < params_.size() && params_[i] > 0 ? params_[i] : fallback;
}

void Vt::csi(char final) {
    if (private_ == '?') {
        if (final == 'h' || final == 'l') mode(final == 'h');
        return;
    }
    if (private_ == '>') {
        if (final == 'c') reply_ += "\x1b[>0;10;1c";
        return;
    }
    if (private_) return;
    if (!intermediates_.empty()) {
        if (intermediates_ == "!" && final == 'p') {
            pen_ = Cell{};
            insert_ = originMode_ = false;
            autowrap_ = cursorVisible_ = true;
            appCursor_ = false;
            top_ = 0;
            bottom_ = rows_ - 1;
            g0Graphics_ = g1Graphics_ = shiftOut_ = false;
        }
        return;
    }
    if (final != 'm' && final != 'n' && final != 'c' && final != 'h' && final != 'l' && final != 't') wrapPending_ = false;

    int n = param(0, 1);
    int p0 = params_.empty() || params_[0] < 0 ? 0 : params_[0];
    auto& s = screen();
    switch (final) {
        case '@': {
            auto& l = s[y_];
            n = std::min(n, cols_ - x_);
            l.insert(l.begin() + x_, n, blank());
            l.resize(cols_);
            break;
        }
        case 'A': y_ = std::max(y_ >= top_ ? top_ : 0, y_ - n); break;
        case 'B':
        case 'e': y_ = std::min(y_ <= bottom_ ? bottom_ : rows_ - 1, y_ + n); break;
        case 'C':
        case 'a': x_ = std::min(cols_ - 1, x_ + n); break;
        case 'D': x_ = std::max(0, std::min(x_, cols_ - 1) - n); break;
        case 'E':
            y_ = std::min(rows_ - 1, y_ + n);
            x_ = 0;
            break;
        case 'F':
            y_ = std::max(0, y_ - n);
            x_ = 0;
            break;
        case 'G':
        case '`': x_ = std::clamp(n - 1, 0, cols_ - 1); break;
        case 'H':
        case 'f': moveTo(param(1, 1) - 1, n - 1); break;
        case 'I':
            for (int i = 0; i < n; ++i) x_ = std::min(cols_ - 1, (x_ / 8 + 1) * 8);
            break;
        case 'Z':
            for (int i = 0; i < n; ++i) x_ = std::max(0, (x_ - 1) / 8 * 8);
            break;
        case 'J':
            if (p0 == 0) {
                clearCells(y_, x_, cols_);
                for (int r = y_ + 1; r < rows_; ++r) clearCells(r, 0, cols_);
            } else if (p0 == 1) {
                for (int r = 0; r < y_; ++r) clearCells(r, 0, cols_);
                clearCells(y_, 0, x_ + 1);
            } else if (p0 == 2) {
                for (int r = 0; r < rows_; ++r) clearCells(r, 0, cols_);
            } else if (p0 == 3) {
                history_.clear();
            }
            break;
        case 'K':
            if (p0 == 0) clearCells(y_, x_, cols_);
            else if (p0 == 1) clearCells(y_, 0, x_ + 1);
            else clearCells(y_, 0, cols_);
            break;
        case 'L':
            if (y_ >= top_ && y_ <= bottom_) scrollDown(y_, bottom_, n);
            x_ = 0;
            break;
        case 'M':
            if (y_ >= top_ && y_ <= bottom_) scrollUp(y_, bottom_, n);
            x_ = 0;
            break;
        case 'P': {
            auto& l = s[y_];
            int cx = std::min(x_, cols_ - 1);
            n = std::min(n, cols_ - cx);
            l.erase(l.begin() + cx, l.begin() + cx + n);
            l.resize(cols_, blank());
            break;
        }
        case 'S': scrollUp(top_, bottom_, n); break;
        case 'T':
            if (params_.size() <= 1) scrollDown(top_, bottom_, n);
            break;
        case 'X': clearCells(y_, x_, x_ + n); break;
        case 'b':
            for (int i = 0; i < std::min(n, 4096); ++i) put(lastChar_);
            break;
        case 'c':
            if (p0 == 0) reply_ += "\x1b[?1;2c";
            break;
        case 'd': y_ = std::clamp(n - 1, 0, rows_ - 1); break;
        case 'h':
        case 'l':
            if (p0 == 4) insert_ = final == 'h';
            break;
        case 'm': sgr(); break;
        case 'n':
            if (p0 == 5) reply_ += "\x1b[0n";
            else if (p0 == 6) {
                char buf[32];
                snprintf(buf, sizeof buf, "\x1b[%d;%dR", (originMode_ ? y_ - top_ : y_) + 1, std::min(x_, cols_ - 1) + 1);
                reply_ += buf;
            }
            break;
        case 'r': {
            int t = param(0, 1) - 1, b = param(1, rows_) - 1;
            if (t < b && b < rows_) {
                top_ = t;
                bottom_ = b;
                moveTo(0, 0);
            }
            break;
        }
        case 's': escape('7'); break;
        case 'u': escape('8'); break;
        default: break;
    }
}

void Vt::sgr() {
    if (params_.empty()) {
        pen_ = Cell{};
        return;
    }
    for (size_t i = 0; i < params_.size(); ++i) {
        int p = params_[i] < 0 ? 0 : params_[i];
        auto colour = [&](uint32_t& target) {
            int kind = i + 1 < params_.size() ? params_[i + 1] : -1;
            if (kind == 5 && i + 2 < params_.size()) {
                target = (uint32_t)std::clamp(params_[i + 2], 0, 255);
                i += 2;
            } else if (kind == 2 && i + 4 < params_.size()) {
                auto c = [&](size_t k) { return (uint32_t)std::clamp(params_[k], 0, 255); };
                target = kRgb | c(i + 2) << 16 | c(i + 3) << 8 | c(i + 4);
                i += 4;
            } else {
                i = params_.size();
            }
        };
        switch (p) {
            case 0: pen_ = Cell{}; break;
            case 1: pen_.attr |= kBold; break;
            case 2: pen_.attr |= kDim; break;
            case 3: pen_.attr |= kItalic; break;
            case 4: pen_.attr |= kUnderline; break;
            case 7: pen_.attr |= kInverse; break;
            case 8: pen_.attr |= kHidden; break;
            case 21:
            case 22: pen_.attr &= ~(kBold | kDim); break;
            case 23: pen_.attr &= ~kItalic; break;
            case 24: pen_.attr &= ~kUnderline; break;
            case 27: pen_.attr &= ~kInverse; break;
            case 28: pen_.attr &= ~kHidden; break;
            case 38: colour(pen_.fg); break;
            case 39: pen_.fg = kDefault; break;
            case 48: colour(pen_.bg); break;
            case 49: pen_.bg = kDefault; break;
            default:
                if (p >= 30 && p <= 37) pen_.fg = p - 30;
                else if (p >= 40 && p <= 47) pen_.bg = p - 40;
                else if (p >= 90 && p <= 97) pen_.fg = p - 90 + 8;
                else if (p >= 100 && p <= 107) pen_.bg = p - 100 + 8;
        }
    }
}

void Vt::setAlt(bool on, bool saveCursor) {
    if (on == alt_) return;
    if (on) {
        if (saveCursor) savedAlt_ = {x_, y_, pen_, g0Graphics_};
        alt_ = true;
        for (auto& l : altLines_) l.assign(cols_, blank());
    } else {
        alt_ = false;
        if (saveCursor) {
            x_ = savedAlt_.x;
            y_ = savedAlt_.y;
            pen_ = savedAlt_.pen;
            g0Graphics_ = savedAlt_.g0Graphics;
        }
    }
    top_ = 0;
    bottom_ = rows_ - 1;
    wrapPending_ = false;
}

void Vt::mode(bool set) {
    for (int p : params_) {
        switch (p) {
            case 1: appCursor_ = set; break;
            case 6:
                originMode_ = set;
                moveTo(0, 0);
                break;
            case 7: autowrap_ = set; break;
            case 25: cursorVisible_ = set; break;
            case 47:
            case 1047: setAlt(set, false); break;
            case 1048:
                if (set) escape('7');
                else escape('8');
                break;
            case 1049: setAlt(set, true); break;
            case 2004: bracketedPaste_ = set; break;
            default: break;
        }
    }
}
