#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx.h"

// Glyph atlas text engine for book and terminal text. Glyphs are rasterized on the CPU into 1024x1024 pages;
// dirty rows of a page are uploaded once per frame, before any text of that frame is drawn, so the
// GPU never waits on a texture that queued draws still use. Text is drawn with one
// SDL_RenderGeometry call per page.
class Glyphs {
public:
    enum Style { Regular = 0, Bold = 1, Italic = 2, BoldItalic = 3 };

    struct Glyph {
        int page = -1;
        int x = 0, y = 0, w = 0, h = 0;  // atlas rectangle
        int left = 0, top = 0;           // bitmap offset from the pen position and baseline
        int advance = 0;
    };

    bool open(const std::string& family, int size);
    void close();
    const Glyph& glyph(int style, uint32_t cp);
    int kerning(int style, uint32_t prev, uint32_t cp);
    int lineSkip() const { return lineSkip_; }
    int ascent() const { return ascent_; }
    int size() const { return size_; }
    const std::string& family() const { return family_; }

    // Queues a glyph at pen position x and baseline y.
    void add(const Glyph& g, float x, float y, Color c);
    void flush(SDL_Renderer* r);
    size_t glyphCount() const { return glyphs_.size(); }
    int pageCount() const { return (int)pages_.size(); }

private:
    struct Page {
        std::vector<uint8_t> px;
        SDL_Texture* tex = nullptr;
        int shelfY = 0, shelfH = 0, cursorX = 0;
        int dirtyTop = 1 << 30, dirtyBottom = -1;
        std::vector<SDL_Vertex> verts;
        std::vector<int> indices;
    };
    static constexpr int kPage = 1024;

    TTF_Font* fontFor(int style, uint32_t cp);
    bool place(int w, int h, int& page, int& x, int& y);

    std::string family_;
    int size_ = 0, lineSkip_ = 0, ascent_ = 0;
    TTF_Font* fonts_[4] = {};
    std::vector<TTF_Font*> fallbacks_;
    bool fallbacksOpened_ = false;
    std::vector<Page> pages_;
    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::unordered_map<uint64_t, int> kerning_;
};
