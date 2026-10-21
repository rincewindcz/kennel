#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

// Terminal state and escape sequence parser: the xterm subset that busybox tools and ncurses with
// TERM=xterm-256color use. No double width characters, no mouse, no reflow on resize.
class Vt {
public:
    // Colours: 0..255 palette indices, kRgb | 0xRRGGBB, or kDefault.
    static constexpr uint32_t kDefault = 0xffffffffu, kRgb = 0x01000000u;
    enum Attr : uint8_t { kBold = 1, kDim = 2, kItalic = 4, kUnderline = 8, kInverse = 16, kHidden = 32 };

    struct Cell {
        uint32_t cp = ' ';
        uint32_t fg = kDefault, bg = kDefault;
        uint8_t attr = 0;
    };
    using Line = std::vector<Cell>;

    Vt();
    void resize(int cols, int rows);
    void feed(std::string_view data);
    std::string takeReply();
    void reset();

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    // Row 0 is the top of the screen; negative rows are the scrollback, -1 the newest line.
    const Line& line(int row) const;
    int historySize() const { return (int)history_.size(); }
    void clearHistory() { history_.clear(); }

    int cursorX() const { return std::min(x_, cols_ - 1); }
    int cursorY() const { return y_; }
    bool cursorVisible() const { return cursorVisible_; }
    bool appCursor() const { return appCursor_; }
    bool bracketedPaste() const { return bracketedPaste_; }
    bool altScreen() const { return alt_; }
    const std::string& title() const { return title_; }

private:
    enum class State { Ground, Escape, Charset, Csi, Osc, OscEscape, Ignore, IgnoreEscape };
    struct Saved {
        int x = 0, y = 0;
        Cell pen;
        bool g0Graphics = false;
    };

    std::vector<Line>& screen() { return alt_ ? altLines_ : lines_; }
    Cell blank() const;
    void put(uint32_t cp);
    void lineFeed();
    void reverseIndex();
    void scrollUp(int top, int bottom, int n, bool keep = false);
    void scrollDown(int top, int bottom, int n);
    void clearCells(int row, int from, int to);
    void escape(char c);
    void csi(char final);
    void sgr();
    void mode(bool set);
    void setAlt(bool on, bool saveCursor);
    void moveTo(int x, int y);
    int param(size_t i, int fallback) const;

    int cols_ = 80, rows_ = 24;
    std::vector<Line> lines_, altLines_;
    std::deque<Line> history_;
    int x_ = 0, y_ = 0;
    bool wrapPending_ = false;
    int top_ = 0, bottom_ = 23;
    Cell pen_;
    Saved saved_, savedAlt_;
    bool alt_ = false;
    bool autowrap_ = true, insert_ = false, originMode_ = false;
    bool cursorVisible_ = true, appCursor_ = false, bracketedPaste_ = false;
    bool g0Graphics_ = false, g1Graphics_ = false, shiftOut_ = false;
    uint32_t lastChar_ = ' ';

    State state_ = State::Ground;
    uint32_t utf8_ = 0;
    int utf8Left_ = 0;
    std::vector<int> params_;
    std::string intermediates_;
    char private_ = 0;
    char charsetTarget_ = 0;
    std::string osc_;
    std::string title_;
    std::string reply_;
};
