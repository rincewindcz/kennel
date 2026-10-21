#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A book reduced to styled paragraphs. Text is UTF-8 with single spaces and no line breaks;
// reading positions are byte offsets into the concatenated paragraph texts, so they stay valid
// when the font or layout changes.
struct Run {
    uint32_t start = 0, length = 0;  // byte range in Paragraph::text
    uint8_t style = 0;               // Run::Bold | Run::Italic
    static constexpr uint8_t Bold = 1, Italic = 2;
};

struct Paragraph {
    enum class Kind : uint8_t { Body, Heading, Break };  // Break: scene break or empty line, no text
    Kind kind = Kind::Body;
    std::string text;
    std::vector<Run> runs;
};

struct Chapter {
    std::string title;
    uint32_t paragraph = 0;
};

struct Book {
    std::string path, title, author, format;
    std::vector<Paragraph> paragraphs;
    std::vector<Chapter> chapters;
    std::vector<uint64_t> offsets;  // byte offset of each paragraph, filled by finish()
    uint64_t size = 0;

    // Normalizes whitespace, drops empty paragraphs, collapses repeated breaks, fixes chapter
    // indices, falls back to headings for chapters and computes offsets.
    void finish();
    int paragraphAt(uint64_t offset) const;
    int chapterAt(int paragraph) const;
};

// Loads by extension: .txt, .epub, .html/.htm/.xhtml, .fb2, .pdb/.mobi/.prc.
bool loadBook(const std::string& path, Book& book, std::string& error);

// Format loaders (fill paragraphs, chapters, title, author; loadBook calls finish()).
bool loadTxt(const std::string& path, Book& book, std::string& error);
bool loadEpub(const std::string& path, Book& book, std::string& error);
bool loadHtml(const std::string& path, Book& book, std::string& error);
bool loadFb2(const std::string& path, Book& book, std::string& error);
bool loadPdb(const std::string& path, Book& book, std::string& error);
