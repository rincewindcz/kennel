#include "glyphs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "paths.h"
#include "platform.h"
#include "util.h"

namespace {

const char* const kStyleNames[] = {"Regular", "Bold", "Italic", "BoldItalic"};

// assets/fonts/read/<Family>-<Style>.ttf or assets/fonts/<Family>-<Style>.ttf; missing styles fall
// back to the family's Regular or Bold, then to Inter, with italics and bold synthesized by SDL_ttf.
TTF_Font* openFile(const std::string& family, const char* style, int size) {
    for (const char* dir : {"fonts/read/", "fonts/"})
        if (TTF_Font* f = TTF_OpenFont(paths.asset(dir + family + "-" + style + ".ttf").c_str(), (float)size)) return f;
    return nullptr;
}

TTF_Font* openStyle(const std::string& family, int style, int size) {
    if (TTF_Font* f = openFile(family, kStyleNames[style], size)) return f;
    bool bold = style & Glyphs::Bold, italic = style & Glyphs::Italic;
    TTF_Font* f = openFile(family, bold ? "Bold" : "Regular", size);
    int synth = italic ? TTF_STYLE_ITALIC : 0;
    if (!f && bold && (f = openFile(family, "Regular", size))) synth |= TTF_STYLE_BOLD;
    if (!f) f = TTF_OpenFont(paths.asset(std::string("fonts/Inter-") + (bold ? "Bold" : "Regular") + ".ttf").c_str(), (float)size);
    if (f && synth) TTF_SetFontStyle(f, synth);
    return f;
}

}  // namespace

bool Glyphs::open(const std::string& family, int size) {
    close();
    family_ = family;
    size_ = size;
    for (int s = 0; s < 4; ++s) {
        fonts_[s] = openStyle(family, s, size);
        if (!fonts_[s]) {
            logf("reading font %s %s: %s", family.c_str(), kStyleNames[s], SDL_GetError());
            return false;
        }
        TTF_SetFontHinting(fonts_[s], TTF_HINTING_LIGHT);
    }
    lineSkip_ = TTF_GetFontLineSkip(fonts_[0]);
    ascent_ = TTF_GetFontAscent(fonts_[0]);
    return true;
}

void Glyphs::close() {
    for (auto& f : fonts_)
        if (f) TTF_CloseFont(f), f = nullptr;
    for (auto* f : fallbacks_) TTF_CloseFont(f);
    fallbacks_.clear();
    fallbacksOpened_ = false;
    for (auto& p : pages_)
        if (p.tex) SDL_DestroyTexture(p.tex);
    pages_.clear();
    glyphs_.clear();
    kerning_.clear();
}

TTF_Font* Glyphs::fontFor(int style, uint32_t cp) {
    if (TTF_FontHasGlyph(fonts_[style], cp)) return fonts_[style];
    if (!fallbacksOpened_) {
        fallbacksOpened_ = true;
        for (const auto& path : platform.fallbackFonts)
            if (TTF_Font* f = TTF_OpenFont(paths.expand(path).c_str(), (float)size_)) fallbacks_.push_back(f);
    }
    for (TTF_Font* f : fallbacks_)
        if (TTF_FontHasGlyph(f, cp)) return f;
    return fonts_[style];
}

bool Glyphs::place(int w, int h, int& page, int& x, int& y) {
    if (w > kPage || h > kPage) return false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!pages_.empty()) {
            Page& p = pages_.back();
            if (p.cursorX + w > kPage) {
                p.shelfY += p.shelfH + 1;
                p.shelfH = 0;
                p.cursorX = 0;
            }
            if (p.shelfY + h <= kPage) {
                page = (int)pages_.size() - 1;
                x = p.cursorX;
                y = p.shelfY;
                p.cursorX += w + 1;
                p.shelfH = std::max(p.shelfH, h);
                return true;
            }
        }
        Page np;
        np.px.assign((size_t)kPage * kPage * 4, 0);
        for (size_t i = 0; i < np.px.size(); i += 4) np.px[i] = np.px[i + 1] = np.px[i + 2] = 255;
        pages_.push_back(std::move(np));
    }
    return false;
}

const Glyphs::Glyph& Glyphs::glyph(int style, uint32_t cp) {
    uint32_t key = (uint32_t)style << 21 | (cp & 0x1fffff);
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return it->second;

    Glyph g;
    TTF_Font* f = fontFor(style, cp);
    int minx = 0, maxx = 0, miny = 0, maxy = 0, adv = 0;
    TTF_GetGlyphMetrics(f, cp, &minx, &maxx, &miny, &maxy, &adv);
    g.advance = adv;
    g.left = minx;
    g.top = maxy;
    TTF_ImageType type = TTF_IMAGE_INVALID;
    SDL_Surface* s = cp == ' ' ? nullptr : TTF_GetGlyphImage(f, cp, &type);
    if (s && s->w > 0 && s->h > 0 && (s->w > 1 || s->h > 1) && place(s->w, s->h, g.page, g.x, g.y)) {
        g.w = s->w;
        g.h = s->h;
        SDL_Surface* argb = s->format == SDL_PIXELFORMAT_ARGB8888 ? s : SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
        Page& p = pages_[g.page];
        if (argb && SDL_LockSurface(argb)) {
            for (int y = 0; y < g.h; ++y) {
                const uint32_t* row = (const uint32_t*)((const uint8_t*)argb->pixels + (size_t)y * argb->pitch);
                uint8_t* dst = &p.px[((size_t)(g.y + y) * kPage + g.x) * 4];
                for (int x = 0; x < g.w; ++x) {
                    uint32_t v = row[x];
                    if (type == TTF_IMAGE_COLOR) {
                        dst[x * 4 + 0] = (v >> 16) & 0xff;
                        dst[x * 4 + 1] = (v >> 8) & 0xff;
                        dst[x * 4 + 2] = v & 0xff;
                    }
                    dst[x * 4 + 3] = (uint8_t)(v >> 24);
                }
            }
            SDL_UnlockSurface(argb);
        }
        if (argb && argb != s) SDL_DestroySurface(argb);
        p.dirtyTop = std::min(p.dirtyTop, g.y);
        p.dirtyBottom = std::max(p.dirtyBottom, g.y + g.h);
    } else {
        g.page = -1;
    }
    if (s) SDL_DestroySurface(s);
    return glyphs_.emplace(key, g).first->second;
}

int Glyphs::kerning(int style, uint32_t prev, uint32_t cp) {
    uint64_t key = (uint64_t)style << 42 | (uint64_t)(prev & 0x1fffff) << 21 | (cp & 0x1fffff);
    auto it = kerning_.find(key);
    if (it != kerning_.end()) return it->second;
    int k = 0;
    TTF_GetGlyphKerning(fonts_[style], prev, cp, &k);
    kerning_.emplace(key, k);
    return k;
}

void Glyphs::add(const Glyph& g, float x, float y, Color c) {
    if (g.page < 0) return;
    Page& p = pages_[g.page];
    float x0 = std::round(x) + g.left, y0 = std::round(y) - g.top;
    float u0 = (float)g.x / kPage, v0 = (float)g.y / kPage, u1 = (float)(g.x + g.w) / kPage, v1 = (float)(g.y + g.h) / kPage;
    SDL_FColor col{c.r, c.g, c.b, c.a};
    int base = (int)p.verts.size();
    p.verts.push_back({{x0, y0}, col, {u0, v0}});
    p.verts.push_back({{x0 + g.w, y0}, col, {u1, v0}});
    p.verts.push_back({{x0 + g.w, y0 + g.h}, col, {u1, v1}});
    p.verts.push_back({{x0, y0 + g.h}, col, {u0, v1}});
    for (int i : {0, 1, 2, 0, 2, 3}) p.indices.push_back(base + i);
}

void Glyphs::flush(SDL_Renderer* r) {
    for (auto& p : pages_) {
        if (!p.tex) {
            p.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kPage, kPage);
            if (!p.tex) continue;
            SDL_SetTextureBlendMode(p.tex, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(p.tex, SDL_SCALEMODE_NEAREST);
            p.dirtyTop = 0;
            p.dirtyBottom = kPage;
        }
        if (p.dirtyBottom > p.dirtyTop) {
            SDL_Rect rect{0, p.dirtyTop, kPage, p.dirtyBottom - p.dirtyTop};
            SDL_UpdateTexture(p.tex, &rect, &p.px[(size_t)p.dirtyTop * kPage * 4], kPage * 4);
            p.dirtyTop = 1 << 30;
            p.dirtyBottom = -1;
        }
    }
    for (auto& p : pages_) {
        if (p.tex && !p.indices.empty())
            SDL_RenderGeometry(r, p.tex, p.verts.data(), (int)p.verts.size(), p.indices.data(), (int)p.indices.size());
        p.verts.clear();
        p.indices.clear();
    }
}
