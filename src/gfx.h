#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct Image;

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
};

inline Color rgb(uint32_t hex, float a = 1.f) {
    return {((hex >> 16) & 0xff) / 255.f, ((hex >> 8) & 0xff) / 255.f, (hex & 0xff) / 255.f, a};
}
inline Color fade(Color c, float a) {
    c.a *= a;
    return c;
}
inline Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
inline Color shade(Color c, float f) { return {c.r * f, c.g * f, c.b * f, c.a}; }

namespace theme {
inline const Color bgTop = rgb(0x171b24), bgBottom = rgb(0x0a0c11);
inline const Color panel = rgb(0x1b2029), panelHi = rgb(0x262c37), line = rgb(0x2e3542);
inline const Color text = rgb(0xf2f4f8), dim = rgb(0xa3abb9), faint = rgb(0x646d7c);
inline const Color good = rgb(0x5bd16e), warn = rgb(0xffb020), bad = rgb(0xff5c5c);
}  // namespace theme

enum class Font { Tiny, Small, SmallBold, Body, Bold, Title, Huge, Mono, Count };
enum class Align { Left, Center, Right };

class Gfx {
public:
    bool init(int logicalW, int logicalH, bool fullscreen);
    void shutdown();
    void beginFrame();
    void endFrame();

    void fill(const SDL_FRect& r, Color c);
    void gradient(const SDL_FRect& r, Color from, Color to, bool horizontal = false);
    void round(const SDL_FRect& r, float radius, Color c);
    void stroke(const SDL_FRect& r, float radius, float width, Color c);
    void shadow(const SDL_FRect& r, float radius, float blur, Color c);
    void glow(const SDL_FRect& r, Color c);
    void image(SDL_Texture* t, const SDL_FRect& dst, float alpha = 1.f, const SDL_FRect* src = nullptr);
    void icon(std::string_view name, float x, float y, float size, Color c);

    float text(Font f, std::string_view s, float x, float y, Color c, float maxW = 0, Align align = Align::Left);
    float measure(Font f, std::string_view s);
    float lineHeight(Font f);
    std::vector<std::string> wrap(Font f, std::string_view s, float maxW, int maxLines);
    float paragraph(Font f, std::string_view s, float x, float y, float maxW, Color c, int maxLines, float gap = 0);
    float button(std::string_view label, float x, float y, float h, Color bg, Color fg);
    float buttonWidth(std::string_view label, float h);

    void pushClip(const SDL_FRect& r);
    void popClip();
    SDL_Texture* texture(const Image& img);
    SDL_Texture* target(int w, int h);
    void setTarget(SDL_Texture* t);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    float W = 640, H = 480;
    std::string rendererName;
    float refresh = 60;
    uint64_t frame = 0;
    double frameMs = 0, frameMsAvg = 0;
    size_t textCount() const { return texts_.size(); }
    double maskMs = 0, iconMs = 0, textMs = 0;

private:
    struct CachedText {
        SDL_Texture* tex;
        int w, h;
        uint64_t used;
    };
    CachedText& cachedText(Font f, std::string_view s);
    std::string ellipsize(Font f, std::string_view s, float maxW);
    SDL_Texture* mask(int kind, int a, int b);
    void nineGrid(SDL_Texture* t, float corner, const SDL_FRect& dst, Color c);

    TTF_Font* fonts_[(int)Font::Count] = {};
    std::unordered_map<std::string, CachedText> texts_;
    std::unordered_map<uint64_t, SDL_Texture*> masks_;
    std::unordered_map<std::string, SDL_Texture*> icons_;
    std::vector<SDL_Rect> clips_;
    uint64_t frameStart_ = 0;
};

extern Gfx gfx;
