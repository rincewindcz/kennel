#include "gfx.h"

#include <algorithm>
#include <cmath>

#include "image.h"
#include "paths.h"
#include "platform.h"
#include "util.h"

Gfx gfx;

namespace {

struct FontSpec {
    const char* file;
    float size;
};
const FontSpec kFonts[] = {
    {"Inter-SemiBold.ttf", 11}, {"Inter-Regular.ttf", 14}, {"Inter-SemiBold.ttf", 14}, {"Inter-Regular.ttf", 17},
    {"Inter-SemiBold.ttf", 17}, {"Inter-SemiBold.ttf", 26}, {"Inter-Bold.ttf", 32},     {"JetBrainsMono-Regular.ttf", 13},
};

enum MaskKind { kRound = 1, kStroke = 2, kShadow = 3 };

float roundBoxDistance(float px, float py, float half, float r) {
    float qx = std::fabs(px - half) - (half - r), qy = std::fabs(py - half) - (half - r);
    float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

}  // namespace

bool Gfx::init(int logicalW, int logicalH, bool fullscreen) {
    SDL_WindowFlags flags = fullscreen ? SDL_WINDOW_FULLSCREEN : 0;
    window = SDL_CreateWindow("kennel", logicalW, logicalH, flags);
    if (!window) {
        logf("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        logf("SDL_CreateRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer, logicalW, logicalH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    W = (float)logicalW;
    H = (float)logicalH;
    rendererName = SDL_GetRendererName(renderer);
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)))
        if (mode->refresh_rate > 0) refresh = mode->refresh_rate;
    SDL_HideCursor();

    if (!TTF_Init()) {
        logf("TTF_Init: %s", SDL_GetError());
        return false;
    }
    engine_ = TTF_CreateRendererTextEngine(renderer);
    std::vector<TTF_Font*> fallbacks;
    for (int i = 0; i < (int)Font::Count; ++i) {
        std::string path = paths.asset(std::string("fonts/") + kFonts[i].file);
        fonts_[i] = TTF_OpenFont(path.c_str(), kFonts[i].size);
        if (!fonts_[i]) {
            logf("font %s: %s", path.c_str(), SDL_GetError());
            return false;
        }
        TTF_SetFontHinting(fonts_[i], TTF_HINTING_LIGHT);
        for (const auto& fb : platform.fallbackFonts) {
            if (TTF_Font* f = TTF_OpenFont(paths.expand(fb).c_str(), kFonts[i].size)) {
                TTF_AddFallbackFont(fonts_[i], f);
                fallbacks.push_back(f);
            }
        }
    }
    logf("renderer %s, %dx%d logical, %.1f Hz", rendererName.c_str(), logicalW, logicalH, refresh);
    return true;
}

void Gfx::shutdown() {
    for (auto& [k, t] : texts_) TTF_DestroyText(t.text);
    texts_.clear();
    if (engine_) TTF_DestroyRendererTextEngine(engine_);
    engine_ = nullptr;
    for (auto& f : fonts_)
        if (f) TTF_CloseFont(f), f = nullptr;
    masks_.clear();
    icons_.clear();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    renderer = nullptr;
    window = nullptr;
    TTF_Quit();
}

void Gfx::beginFrame() {
    frameStart_ = SDL_GetTicksNS();
    ++frame;
    SDL_SetRenderDrawColorFloat(renderer, 0, 0, 0, 1);
    SDL_RenderClear(renderer);
}

void Gfx::endFrame() {
    frameMs = (SDL_GetTicksNS() - frameStart_) / 1e6;
    frameMsAvg = frameMsAvg * 0.9 + frameMs * 0.1;
    SDL_RenderPresent(renderer);
    if (texts_.size() > 400) {
        for (auto it = texts_.begin(); it != texts_.end();) {
            if (frame - it->second.used > 120) {
                TTF_DestroyText(it->second.text);
                it = texts_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void Gfx::fill(const SDL_FRect& r, Color c) {
    if (c.a <= 0) return;
    SDL_SetRenderDrawColorFloat(renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(renderer, &r);
}

void Gfx::gradient(const SDL_FRect& r, Color from, Color to, bool horizontal) {
    SDL_FColor a{from.r, from.g, from.b, from.a}, b{to.r, to.g, to.b, to.a};
    SDL_Vertex v[4] = {
        {{r.x, r.y}, a, {0, 0}},
        {{r.x + r.w, r.y}, horizontal ? b : a, {0, 0}},
        {{r.x + r.w, r.y + r.h}, b, {0, 0}},
        {{r.x, r.y + r.h}, horizontal ? a : b, {0, 0}},
    };
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
}

SDL_Texture* Gfx::mask(int kind, int a, int b) {
    uint64_t key = (uint64_t)kind << 48 | (uint64_t)a << 24 | (uint64_t)b;
    auto it = masks_.find(key);
    if (it != masks_.end()) return it->second;

    int r = a;
    int pad = kind == kShadow ? b : 0;
    float half = r + 2.f;  // corner cells of r + 1 pixels leave a uniform 2-pixel middle
    int size = (int)(2 * (half + pad));
    Image img;
    img.w = img.h = size;
    img.px.assign((size_t)size * size * 4, 255);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float d = roundBoxDistance(x + 0.5f - pad, y + 0.5f - pad, half, (float)r);
            float cov;
            if (kind == kRound) cov = std::clamp(0.5f - d, 0.f, 1.f);
            else if (kind == kStroke) cov = std::clamp(0.5f - d, 0.f, 1.f) * std::clamp(0.5f + d + b, 0.f, 1.f);
            else {
                float t = std::clamp((b - d) / (2.f * b), 0.f, 1.f);
                cov = t * t * (3 - 2 * t);
            }
            img.px[((size_t)y * size + x) * 4 + 3] = (uint8_t)std::lround(cov * 255);
        }
    }
    SDL_Texture* t = texture(img);
    masks_[key] = t;
    return t;
}

void Gfx::nineGrid(SDL_Texture* t, float corner, const SDL_FRect& dst, Color c) {
    if (!t || c.a <= 0) return;
    SDL_SetTextureColorModFloat(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaModFloat(t, c.a);
    SDL_RenderTexture9Grid(renderer, t, nullptr, corner, corner, corner, corner, 1.f, &dst);
}

void Gfx::round(const SDL_FRect& r, float radius, Color c) {
    int rad = (int)std::floor(std::min({radius, r.w / 2 - 1, r.h / 2 - 1}));
    if (rad <= 0) return fill(r, c);
    nineGrid(mask(kRound, rad, 0), rad + 1.f, r, c);
}

void Gfx::stroke(const SDL_FRect& r, float radius, float width, Color c) {
    int rad = (int)std::floor(std::min({radius, r.w / 2 - 1, r.h / 2 - 1}));
    rad = std::max(rad, (int)std::ceil(width));
    nineGrid(mask(kStroke, rad, (int)std::lround(width)), rad + 1.f, r, c);
}

void Gfx::shadow(const SDL_FRect& r, float radius, float blur, Color c) {
    int rad = (int)std::floor(std::min({radius, r.w / 2 - 1, r.h / 2 - 1}));
    rad = std::max(rad, 1);
    int b = std::max(1, (int)std::lround(blur));
    SDL_FRect dst{r.x - b, r.y - b, r.w + 2 * b, r.h + 2 * b};
    nineGrid(mask(kShadow, rad, b), rad + 1.f + b, dst, c);
}

void Gfx::image(SDL_Texture* t, const SDL_FRect& dst, float alpha, const SDL_FRect* src) {
    if (!t || alpha <= 0) return;
    SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
    SDL_GetTextureBlendMode(t, &mode);
    float m = mode == SDL_BLENDMODE_BLEND_PREMULTIPLIED ? alpha : 1.f;
    SDL_SetTextureColorModFloat(t, m, m, m);
    SDL_SetTextureAlphaModFloat(t, alpha);
    SDL_RenderTexture(renderer, t, src, &dst);
}

void Gfx::icon(std::string_view name, float x, float y, float size, Color c) {
    int bucket = size <= 22 ? 20 : 32;
    std::string key = std::to_string(bucket) + "/" + std::string(name);
    auto it = icons_.find(key);
    if (it == icons_.end()) {
        Image img;
        SDL_Texture* t = loadImage(paths.asset("icons/" + key + ".png"), img) ? texture(img) : nullptr;
        if (!t) logf("missing icon %s", key.c_str());
        it = icons_.emplace(key, t).first;
    }
    if (!it->second || c.a <= 0) return;
    SDL_SetTextureColorModFloat(it->second, c.r, c.g, c.b);
    SDL_SetTextureAlphaModFloat(it->second, c.a);
    SDL_FRect dst{std::round(x), std::round(y), size, size};
    SDL_RenderTexture(renderer, it->second, nullptr, &dst);
}

Gfx::CachedText& Gfx::cachedText(Font f, std::string_view s) {
    std::string key;
    key.reserve(s.size() + 2);
    key += (char)('0' + (int)f);
    key += s;
    auto it = texts_.find(key);
    if (it == texts_.end()) {
        TTF_Text* t = TTF_CreateText(engine_, fonts_[(int)f], s.data(), s.size());
        int w = 0, h = 0;
        if (t) TTF_GetTextSize(t, &w, &h);
        it = texts_.emplace(std::move(key), CachedText{t, w, h, frame}).first;
    }
    it->second.used = frame;
    return it->second;
}

float Gfx::measure(Font f, std::string_view s) {
    if (s.empty()) return 0;
    int w = 0, h = 0;
    TTF_GetStringSize(fonts_[(int)f], s.data(), s.size(), &w, &h);
    return (float)w;
}

float Gfx::lineHeight(Font f) { return (float)TTF_GetFontHeight(fonts_[(int)f]); }

std::string Gfx::ellipsize(Font f, std::string_view s, float maxW) {
    if (measure(f, s) <= maxW) return std::string(s);
    static const std::string dots = "\xE2\x80\xA6";
    int avail = (int)(maxW - measure(f, dots));
    if (avail <= 0) return dots;
    int measuredW = 0;
    size_t len = 0;
    TTF_MeasureString(fonts_[(int)f], s.data(), s.size(), avail, &measuredW, &len);
    std::string out(s.substr(0, len));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

float Gfx::text(Font f, std::string_view s, float x, float y, Color c, float maxW, Align align) {
    if (s.empty() || c.a <= 0) return 0;
    std::string clipped;
    if (maxW > 0 && measure(f, s) > maxW) {
        clipped = ellipsize(f, s, maxW);
        s = clipped;
    }
    CachedText& t = cachedText(f, s);
    if (!t.text) return 0;
    float dx = align == Align::Center ? -t.w / 2.f : align == Align::Right ? -(float)t.w : 0.f;
    TTF_SetTextColorFloat(t.text, c.r, c.g, c.b, c.a);
    TTF_DrawRendererText(t.text, std::round(x + dx), std::round(y));
    return (float)t.w;
}

std::vector<std::string> Gfx::wrap(Font f, std::string_view s, float maxW, int maxLines) {
    std::vector<std::string> lines;
    std::string cur;
    size_t pos = 0;
    bool more = false;
    while (pos < s.size()) {
        size_t sp = s.find_first_of(" \n", pos);
        std::string word(s.substr(pos, sp == std::string_view::npos ? std::string_view::npos : sp - pos));
        bool newline = sp != std::string_view::npos && s[sp] == '\n';
        pos = sp == std::string_view::npos ? s.size() : sp + 1;
        std::string trial = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && measure(f, trial) > maxW) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = trial;
        }
        if (newline) {
            lines.push_back(cur);
            cur.clear();
        }
        if (maxLines > 0 && (int)lines.size() >= maxLines) {
            more = pos < s.size() || !cur.empty();
            break;
        }
    }
    if (!cur.empty() && (maxLines <= 0 || (int)lines.size() < maxLines)) lines.push_back(cur);
    if (maxLines > 0 && (int)lines.size() > maxLines) lines.resize(maxLines);
    if (more && !lines.empty()) lines.back() = ellipsize(f, lines.back() + " " + std::string(s.substr(std::min(pos, s.size()))), maxW);
    for (auto& l : lines)
        if (measure(f, l) > maxW) l = ellipsize(f, l, maxW);
    return lines;
}

float Gfx::paragraph(Font f, std::string_view s, float x, float y, float maxW, Color c, int maxLines, float gap) {
    float lh = lineHeight(f) + gap;
    auto lines = wrap(f, s, maxW, maxLines);
    for (size_t i = 0; i < lines.size(); ++i) text(f, lines[i], x, y + i * lh, c);
    return lines.size() * lh;
}

float Gfx::buttonWidth(std::string_view label, float h) {
    if (label.size() == 1) return h;
    return std::max(h, measure(Font::Tiny, label) + h * 0.8f);
}

float Gfx::button(std::string_view label, float x, float y, float h, Color bg, Color fg) {
    float w = buttonWidth(label, h);
    round({x, y, w, h}, h / 2, bg);
    Font f = label.size() == 1 ? Font::SmallBold : Font::Tiny;
    text(f, label, x + w / 2, y + (h - lineHeight(f)) / 2, fg, 0, Align::Center);
    return w;
}

void Gfx::pushClip(const SDL_FRect& r) {
    SDL_Rect rect{(int)std::floor(r.x), (int)std::floor(r.y), (int)std::ceil(r.w), (int)std::ceil(r.h)};
    if (!clips_.empty()) {
        SDL_Rect out;
        SDL_GetRectIntersection(&clips_.back(), &rect, &out);
        rect = out;
    }
    clips_.push_back(rect);
    SDL_SetRenderClipRect(renderer, &rect);
}

void Gfx::popClip() {
    clips_.pop_back();
    SDL_SetRenderClipRect(renderer, clips_.empty() ? nullptr : &clips_.back());
}

SDL_Texture* Gfx::texture(const Image& img) {
    if (img.empty()) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, img.px.data(), img.w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

SDL_Texture* Gfx::target(int w, int h) {
    SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) return nullptr;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

void Gfx::setTarget(SDL_Texture* t) {
    SDL_SetRenderTarget(renderer, t);
    if (t) {
        SDL_SetRenderDrawColorFloat(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
    }
}
