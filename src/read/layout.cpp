#include "layout.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>

uint32_t decodeUtf8(const std::string& s, uint32_t& i) {
    unsigned char c = s[i];
    int extra = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
    if (extra < 0 || i + extra >= s.size() + (extra == 0 ? 1 : 0)) {
        ++i;
        return extra == 0 ? c : 0xfffd;
    }
    uint32_t cp = extra == 0 ? c : c & (0x3f >> extra);
    for (int k = 1; k <= extra; ++k) {
        unsigned char cc = s[i + k];
        if ((cc & 0xc0) != 0x80) {
            ++i;
            return 0xfffd;
        }
        cp = cp << 6 | (cc & 0x3f);
    }
    i += extra + 1;
    return cp;
}

void Layout::reset(const Book* book, Glyphs* glyphs, const ReadSettings* settings, float width) {
    book_ = book;
    glyphs_ = glyphs;
    settings_ = settings;
    width_ = width;
    lines_.assign(book->paragraphs.size(), {});
    built_.assign(book->paragraphs.size(), 0);
    styledParagraph_ = -1;
    scanned_ = 0;
    firstLine_.clear();
}

void Layout::styles(int paragraph) {
    if (styledParagraph_ == paragraph) return;
    styledParagraph_ = paragraph;
    const Paragraph& p = book_->paragraphs[paragraph];
    style_.assign(p.text.size() + 1, 0);
    if (settings_->format != ReadSettings::Format::Styled) return;
    uint8_t base = p.kind == Paragraph::Kind::Heading ? Glyphs::Bold : 0;
    std::fill(style_.begin(), style_.end(), base);
    for (const Run& r : p.runs) {
        uint8_t st = (r.style & Run::Bold ? Glyphs::Bold : 0) | (r.style & Run::Italic ? Glyphs::Italic : 0);
        for (uint32_t i = r.start; i < r.start + r.length && i < p.text.size(); ++i) style_[i] |= st;
    }
}

float Layout::spaceWidth(int style) { return (float)glyphs_->glyph(style, ' ').advance; }

float Layout::measure(const std::string& text, uint32_t a, uint32_t b) {
    float w = 0;
    uint32_t prev = 0;
    int prevStyle = -1;
    for (uint32_t i = a; i < b;) {
        int st = style_[i];
        uint32_t cp = decodeUtf8(text, i);
        if (prev && st == prevStyle) w += glyphs_->kerning(st, prev, cp);
        w += glyphs_->glyph(st, cp).advance;
        prev = cp;
        prevStyle = st;
    }
    return w;
}

const std::vector<Line>& Layout::lines(int paragraph) {
    if (!built_[paragraph]) {
        build(paragraph, lines_[paragraph]);
        built_[paragraph] = 1;
    }
    return lines_[paragraph];
}

void Layout::build(int index, std::vector<Line>& out) {
    out.clear();
    const Paragraph& p = book_->paragraphs[index];
    bool styled = settings_->format == ReadSettings::Format::Styled;
    bool spacing = settings_->paragraphs == ReadSettings::Paragraphs::Spacing;
    Paragraph::Kind prevKind = index > 0 ? book_->paragraphs[index - 1].kind : Paragraph::Kind::Heading;
    uint32_t len = (uint32_t)p.text.size();

    if (p.kind == Paragraph::Kind::Break) {
        Line l;
        l.kind = styled ? Line::Kind::Stars : Line::Kind::Blank;
        out.push_back(l);
        if (spacing) out.push_back({Line::Kind::Blank, 0, 0});
        return;
    }
    if (p.kind == Paragraph::Kind::Heading && index > 0 && prevKind == Paragraph::Kind::Body && !spacing)
        out.push_back({Line::Kind::Blank, 0, 0});

    styles(index);
    float em = (float)glyphs_->size();
    float indent = !spacing && p.kind == Paragraph::Kind::Body && prevKind == Paragraph::Kind::Body ? std::round(em * 1.5f) : 0;
    bool justify = settings_->align == ReadSettings::Align::Justify;

    size_t firstText = out.size();
    uint32_t lineStart = 0, lineEnd = 0;
    float lineW = 0;
    bool empty = true;
    auto avail = [&] { return width_ - (out.size() == firstText ? indent : 0); };
    auto emit = [&](uint32_t nextStart) {
        Line l;
        l.start = lineStart;
        l.end = lineEnd;
        l.x = out.size() == firstText ? indent : 0;
        l.gap = lineW;  // natural width, turned into the justification gap below
        out.push_back(l);
        lineStart = nextStart;
        lineW = 0;
        empty = true;
    };

    uint32_t i = 0;
    while (i < len) {
        uint32_t ws = i, we = i;
        while (we < len && p.text[we] != ' ') ++we;
        float ww = measure(p.text, ws, we);
        float sw = empty ? 0 : spaceWidth(style_[ws > 0 ? ws - 1 : 0]);
        if (!empty && lineW + sw + ww > avail()) emit(ws);
        if (empty && ww > avail()) {
            // A word longer than the line: break it between characters.
            uint32_t c = ws;
            float w = 0;
            lineStart = ws;
            while (c < we) {
                uint32_t n = c;
                decodeUtf8(p.text, n);
                float cw = measure(p.text, c, n);
                if (w + cw > avail() && c > lineStart) {
                    lineEnd = c;
                    lineW = w;
                    emit(c);
                    w = 0;
                }
                w += cw;
                c = n;
            }
            lineEnd = we;
            lineW = w;
            empty = false;
        } else {
            if (empty) lineStart = ws;
            lineW += sw + ww;
            lineEnd = we;
            empty = false;
        }
        i = we < len ? we + 1 : we;
    }
    if (!empty || out.size() == firstText) {
        if (empty) lineStart = lineEnd = 0;
        emit(len);
    }

    for (size_t k = firstText; k < out.size(); ++k) {
        Line& l = out[k];
        float natural = l.gap;
        l.gap = 0;
        bool last = k + 1 == out.size();
        if (!justify || last) continue;
        int spaces = 0;
        for (uint32_t c = l.start; c < l.end; ++c) spaces += p.text[c] == ' ';
        float lineAvail = width_ - l.x;
        if (spaces > 0 && natural < lineAvail) {
            float gap = (lineAvail - natural) / spaces;
            if (gap <= em * 1.2f) l.gap = gap;
        }
    }

    if (p.kind == Paragraph::Kind::Heading || spacing) out.push_back({Line::Kind::Blank, len, len});
}

void Layout::draw(int paragraph, const Line& line, float x, float baseline, Color c) {
    if (line.kind == Line::Kind::Blank) return;
    if (line.kind == Line::Kind::Stars) {
        static const std::string stars = "*   *   *";
        float w = 0;
        for (char ch : stars) w += glyphs_->glyph(0, (unsigned char)ch).advance;
        float px = x + std::round((width_ - w) / 2);
        for (char ch : stars) {
            const auto& g = glyphs_->glyph(0, (unsigned char)ch);
            glyphs_->add(g, px, baseline, c);
            px += g.advance;
        }
        return;
    }
    const Paragraph& p = book_->paragraphs[paragraph];
    styles(paragraph);
    float px = x + line.x;
    uint32_t prev = 0;
    int prevStyle = -1;
    for (uint32_t i = line.start; i < line.end;) {
        int st = style_[i];
        uint32_t cp = decodeUtf8(p.text, i);
        if (cp == ' ') {
            px += glyphs_->glyph(st, ' ').advance + line.gap;
            prev = 0;
            continue;
        }
        if (prev && st == prevStyle) px += glyphs_->kerning(st, prev, cp);
        const auto& g = glyphs_->glyph(st, cp);
        glyphs_->add(g, px, baseline, c);
        px += g.advance;
        prev = cp;
        prevStyle = st;
    }
}

bool Layout::next(LinePos& pos) {
    if (pos.line + 1 < (int)lines(pos.paragraph).size()) {
        ++pos.line;
        return true;
    }
    if (pos.paragraph + 1 >= (int)book_->paragraphs.size()) return false;
    ++pos.paragraph;
    pos.line = 0;
    return true;
}

bool Layout::prev(LinePos& pos) {
    if (pos.line > 0) {
        --pos.line;
        return true;
    }
    if (pos.paragraph == 0) return false;
    --pos.paragraph;
    pos.line = (int)lines(pos.paragraph).size() - 1;
    return true;
}

uint64_t Layout::offset(const LinePos& pos) {
    if (book_->paragraphs.empty()) return 0;
    const auto& ls = lines(pos.paragraph);
    uint32_t local = pos.line < (int)ls.size() ? ls[pos.line].start : 0;
    return book_->offsets[pos.paragraph] + local;
}

LinePos Layout::find(uint64_t offset) {
    LinePos pos;
    if (book_->paragraphs.empty()) return pos;
    pos.paragraph = book_->paragraphAt(offset);
    uint64_t local = offset - book_->offsets[pos.paragraph];
    const auto& ls = lines(pos.paragraph);
    for (size_t i = 0; i < ls.size(); ++i) {
        if (ls[i].kind != Line::Kind::Text) continue;
        if (ls[i].start <= local) pos.line = (int)i;
    }
    if (local == 0) pos.line = 0;
    return pos;
}

bool Layout::buildAll(double budget) {
    if (complete()) return true;
    int n = (int)book_->paragraphs.size();
    uint64_t end = SDL_GetTicksNS() + (uint64_t)(budget * 1e9);
    while (scanned_ < n) {
        lines(scanned_++);
        if (scanned_ % 32 == 0 && SDL_GetTicksNS() >= end) return false;
    }
    firstLine_.assign(n + 1, 0);
    for (int i = 0; i < n; ++i) firstLine_[i + 1] = firstLine_[i] + (int)lines_[i].size();
    return true;
}
