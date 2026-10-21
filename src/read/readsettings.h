#pragma once

#include <cstdint>
#include <string>

#include "gfx.h"

struct ReadSettings {
    enum class Margins { Narrow, Normal, Wide };
    enum class Align { Left, Justify };
    enum class Format { Plain, Styled };
    enum class Paragraphs { Indent, Spacing };
    enum class Theme { Dark, Sepia, Light, Custom };

    std::string family = "Literata";
    int size = 22;
    int spacing = 13;  // line spacing in tenths
    Margins margins = Margins::Normal;
    Align align = Align::Left;
    Format format = Format::Styled;
    Paragraphs paragraphs = Paragraphs::Indent;
    Theme theme = Theme::Dark;
    bool statusBar = true;
    uint32_t customBg = 0x1e1e1e, customFg = 0xd4d4d4;

    void load(const std::string& path);
    bool save() const;
    int marginPx() const;
    Color background() const;
    Color text() const;
    Color dim() const;

    std::string path;
};

extern const char* const kReadFamilies[];
extern const int kReadFamilyCount;
