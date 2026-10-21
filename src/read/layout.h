#pragma once

#include <cstdint>
#include <vector>

#include "book.h"
#include "glyphs.h"
#include "readsettings.h"

// Line breaking on a fixed grid: every paragraph becomes whole lines, including the empty lines
// that separate paragraphs and headings, so scrolling always moves by one line.
struct Line {
    enum class Kind : uint8_t { Text, Blank, Stars };
    Kind kind = Kind::Text;
    uint32_t start = 0, end = 0;  // byte range in the paragraph text
    float x = 0;                  // indent or centering offset
    float gap = 0;                // extra space per space character when justified
};

struct LinePos {
    int paragraph = 0, line = 0;
};

class Layout {
public:
    void reset(const Book* book, Glyphs* glyphs, const ReadSettings* settings, float width);
    const std::vector<Line>& lines(int paragraph);
    void draw(int paragraph, const Line& line, float x, float baseline, Color c);

    bool next(LinePos& p);
    bool prev(LinePos& p);
    uint64_t offset(const LinePos& p);
    LinePos find(uint64_t offset);

private:
    void build(int paragraph, std::vector<Line>& out);
    void styles(int paragraph);
    float measure(const std::string& text, uint32_t a, uint32_t b);
    float spaceWidth(int style);

    const Book* book_ = nullptr;
    Glyphs* glyphs_ = nullptr;
    const ReadSettings* settings_ = nullptr;
    float width_ = 0;
    std::vector<std::vector<Line>> lines_;
    std::vector<uint8_t> built_;
    std::vector<uint8_t> style_;  // per byte of the paragraph being built or drawn
    int styledParagraph_ = -1;
};

uint32_t decodeUtf8(const std::string& s, uint32_t& i);
