#include "art.h"

#include <algorithm>
#include <cmath>
#include <sys/stat.h>

#include "anim.h"
#include "catalog.h"
#include "cover.h"
#include "gfx.h"
#include "util.h"

ArtCache art;

namespace {
constexpr size_t kMaxSlots = 64;
constexpr int kComposePerFrame = 3;
constexpr int kUploadsPerFrame = 3;
constexpr float kFadeIn = 0.22f;

// Book files yield their embedded cover, everything else is an image file.
bool loadArt(const std::string& path, Image& out) {
    if (!hasEmbeddedCover(path)) return loadImage(path, out);
    std::string bytes;
    return extractCover(path, bytes) && loadImageMemory(bytes, out);
}

// Background: the art blurred and darkened; the art itself contained in the left column.
Image poster(const Image& src) {
    Image card = cover(src, ArtCache::kW, ArtCache::kH);
    blur(card, 6, 3);
    darken(card, 0.42f);
    Image art = contain(src, ArtCache::kPosterW, ArtCache::kPosterH, 2.f);
    roundCorners(art, 6);
    int x = ArtCache::kPosterX + (ArtCache::kPosterW - art.w) / 2, y = (ArtCache::kH - art.h) / 2;

    Image shade;
    shade.w = art.w + 16;
    shade.h = art.h + 16;
    shade.px.assign((size_t)shade.w * shade.h * 4, 0);
    for (int sy = 8; sy < 8 + art.h; ++sy)
        for (int sx = 8; sx < 8 + art.w; ++sx) shade.px[((size_t)sy * shade.w + sx) * 4 + 3] = 150;
    blur(shade, 4, 2);
    composite(card, shade, x - 8 + 1, y - 8 + 3);
    composite(card, art, x, y);
    roundCorners(card, ArtCache::kRadius);
    return card;
}
}  // namespace

void ArtCache::start(const std::string& cacheDir) {
    dir_ = cacheDir + "/art";
    makeDirs(dir_);
    quit_ = false;
    thread_ = std::thread(&ArtCache::worker, this);
}

void ArtCache::stop() {
    {
        std::lock_guard lock(mutex_);
        quit_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    clear();
}

void ArtCache::clear() {
    for (auto& [k, s] : slots_)
        if (s.tex) SDL_DestroyTexture(s.tex);
    slots_.clear();
    for (auto& [k, t] : fixed_)
        if (t) SDL_DestroyTexture(t);
    fixed_.clear();
    std::lock_guard lock(mutex_);
    results_.clear();
    inflight_.clear();
}

std::string ArtCache::diskPath(const std::string& path, const char* tag) const {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return {};
    std::string id = strprintf("%s|%lld|%lld|%dx%d|1", path.c_str(), (long long)st.st_size, (long long)st.st_mtime, kW, kH);
    return strprintf("%s/%016llx.%s.qoi", dir_.c_str(), (unsigned long long)fnv1a(id), tag);
}

ArtCache::Result ArtCache::process(const Job& job) {
    Result r;
    r.key = job.key;
    if (job.kind == kBackdrop) {
        std::string cached = diskPath(job.path, "b");
        if (!cached.empty() && loadQoi(cached, r.img)) {
            r.ok = true;
            return r;
        }
        Image src;
        if (!loadArt(job.path, src)) return r;
        r.img = cover(src, kBackW, kBackH);
        blur(r.img, 3, 3);
        r.ok = true;
        if (!cached.empty()) saveQoi(cached, r.img);
        return r;
    }

    const std::pair<const char*, Mode> tags[] = {{"c", Mode::Cover}, {"i", Mode::Icon}, {"p", Mode::Poster}};
    for (const auto& [tag, mode] : tags) {
        std::string file = diskPath(job.path, tag);
        if (file.empty()) return r;
        if (loadQoi(file, r.img)) {
            r.ok = true;
            r.mode = mode;
            return r;
        }
    }
    Image src;
    if (!loadArt(job.path, src)) return r;
    float aspect = (float)src.w / src.h, want = (float)kW / kH;
    bool large = src.w >= kW / 2 && src.h >= kH / 2;
    bool fits = std::fabs(aspect / want - 1.f) < 0.35f;
    if (large && fits) {
        r.img = cover(src, kW, kH);
        roundCorners(r.img, kRadius);
        r.mode = Mode::Cover;
    } else if (aspect < 0.85f && src.h >= kPosterH / 2) {
        r.img = poster(src);
        r.mode = Mode::Poster;
    } else {
        r.img = contain(src, kW - 80, kH - 76, 2.f);
        r.mode = Mode::Icon;
    }
    saveQoi(diskPath(job.path, r.mode == Mode::Cover ? "c" : r.mode == Mode::Poster ? "p" : "i"), r.img);
    r.ok = true;
    return r;
}

void ArtCache::worker() {
    while (true) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        Result r = process(job);
        {
            std::lock_guard lock(mutex_);
            inflight_.erase(job.key);
            results_.push_back(std::move(r));
        }
        SDL_Event ev{};
        ev.type = SDL_EVENT_USER;
        SDL_PushEvent(&ev);
    }
}

ArtCache::Slot& ArtCache::slot(const std::string& key) {
    Slot& s = slots_[key];
    s.used = gfx.frame;
    return s;
}

void ArtCache::want(const std::string& key, const std::string& path, Kind kind) {
    Slot& s = slot(key);
    if (s.tex || s.failed || s.requested) return;
    for (const auto& w : wants_)
        if (w.key == key) return;
    wants_.push_back({key, path, kind});
}

void ArtCache::prefetch(const Entry& e) {
    if (!e.art.empty()) want("c:" + e.art, e.art, kCover);
}

ArtCache::Card ArtCache::card(const Entry& e, const Category* c) {
    Card out;
    if (!e.art.empty()) {
        std::string key = "c:" + e.art;
        want(key, e.art, kCover);
        Slot& s = slot(key);
        auto fadeIn = [&] {
            float a = std::clamp((float)((anim::now() - s.readyAt) / std::max(kFadeIn * anim::scale(), 0.001f)), 0.f, 1.f);
            if (a < 1.f) anim::keepAlive();
            return a;
        };
        if (s.tex && s.mode == Mode::Cover) {
            out.art = s.tex;
            out.artAlpha = fadeIn();
            if (out.artAlpha >= 1.f) return out;
        } else if (s.tex && s.mode == Mode::Poster) {
            Slot& sp = slot("sp:" + e.id);
            if (!sp.tex && composedThisFrame_ < kComposePerFrame) {
                sp.tex = composePoster(e, s.tex);
                ++composedThisFrame_;
            }
            if (sp.tex) {
                out.art = sp.tex;
                out.artAlpha = fadeIn();
                if (out.artAlpha >= 1.f) return out;
            }
        } else if (s.tex && s.mode == Mode::Icon) {
            std::string skey = "si:" + e.id;
            Slot& st = slot(skey);
            if (!st.tex && composedThisFrame_ < kComposePerFrame) {
                float iw, ih;
                SDL_GetTextureSize(s.tex, &iw, &ih);
                st.tex = compose(e, c, s.tex, (int)iw, (int)ih);
                ++composedThisFrame_;
            }
            out.stock = st.tex;
            return out;
        }
    }
    std::string skey = "s:" + e.id;
    Slot& st = slot(skey);
    if (!st.tex && composedThisFrame_ < kComposePerFrame) {
        st.tex = compose(e, c, nullptr, 0, 0);
        ++composedThisFrame_;
    }
    out.stock = st.tex;
    return out;
}

SDL_Texture* ArtCache::backdrop(const Entry& e, const Category* c, float& alpha) {
    std::string fallback = !e.fallbackArt.empty() ? e.fallbackArt : c ? c->image : "";
    auto own = slots_.find("c:" + e.art);
    bool ownFailed = own != slots_.end() && own->second.failed;
    std::string path = !e.art.empty() && !ownFailed ? e.art : fallback;
    alpha = 0;
    if (path.empty()) return nullptr;
    std::string key = "b:" + path;
    want(key, path, kBackdrop);
    Slot& s = slot(key);
    if (!s.tex) return nullptr;
    alpha = std::clamp((float)((anim::now() - s.readyAt) / std::max(0.3f * anim::scale(), 0.001f)), 0.f, 1.f);
    if (alpha < 1.f) anim::keepAlive();
    return s.tex;
}

void ArtCache::pump() {
    composedThisFrame_ = 0;
    std::vector<Result> done;
    std::vector<std::string> dropped, queued;
    {
        std::lock_guard lock(mutex_);
        // This frame's requests replace the queue: what scrolled away is no longer decoded.
        // Queued jobs for slots drawn this frame stay; the rest scrolled away and are dropped.
        std::deque<Job> keep;
        for (auto& q : queue_) {
            auto it = slots_.find(q.key);
            if (it != slots_.end() && it->second.used == gfx.frame) {
                keep.push_back(std::move(q));
            } else {
                inflight_.erase(q.key);
                dropped.push_back(q.key);
            }
        }
        queue_ = std::move(keep);
        for (auto& w : wants_) {
            if (inflight_.count(w.key)) continue;
            inflight_[w.key] = true;
            queued.push_back(w.key);
            queue_.push_back(std::move(w));
        }
        size_t n = std::min(results_.size(), (size_t)kUploadsPerFrame);
        done.assign(std::make_move_iterator(results_.begin()), std::make_move_iterator(results_.begin() + n));
        results_.erase(results_.begin(), results_.begin() + n);
    }
    wants_.clear();
    for (const auto& k : dropped) {
        auto it = slots_.find(k);
        if (it != slots_.end()) it->second.requested = false;
    }
    for (const auto& k : queued) slots_[k].requested = true;
    if (!queued.empty()) cv_.notify_one();

    for (auto& r : done) {
        auto it = slots_.find(r.key);
        if (it == slots_.end()) continue;
        Slot& s = it->second;
        s.requested = false;
        if (!r.ok) {
            s.failed = true;
            continue;
        }
        s.tex = gfx.texture(r.img);
        s.mode = r.mode;
        s.readyAt = anim::now();
        s.failed = !s.tex;
    }
    evict();
}

bool ArtCache::busy() {
    std::lock_guard lock(mutex_);
    return !results_.empty();
}

void ArtCache::evict() {
    if (slots_.size() <= kMaxSlots) return;
    std::vector<std::pair<uint64_t, std::string>> order;
    for (auto& [k, s] : slots_)
        if (gfx.frame - s.used > 2 && !s.requested) order.emplace_back(s.used, k);
    std::sort(order.begin(), order.end());
    for (size_t i = 0; i < order.size() && slots_.size() > kMaxSlots; ++i) {
        auto it = slots_.find(order[i].second);
        if (it->second.tex) SDL_DestroyTexture(it->second.tex);
        slots_.erase(it);
    }
}

SDL_Texture* ArtCache::categoryImage(const std::string& path) {
    std::string key = "cat:" + path;
    auto it = fixed_.find(key);
    if (it != fixed_.end()) return it->second;
    Image src, img;
    SDL_Texture* t = nullptr;
    if (loadImage(path, src)) {
        img = cover(src, kW, kH);
        roundCorners(img, kRadius);
        t = gfx.texture(img);
    }
    fixed_[key] = t;
    return t;
}

SDL_Texture* ArtCache::gradientCard(uint32_t color) {
    std::string key = strprintf("grad:%06x", color);
    auto it = fixed_.find(key);
    if (it != fixed_.end()) return it->second;
    Color top = rgb(color), bottom = shade(top, 0.38f);
    Image img;
    img.w = kW;
    img.h = kH;
    img.px.resize((size_t)kW * kH * 4);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            float t = std::clamp((y * 0.8f + x * 0.35f) / (kH * 0.8f + kW * 0.35f), 0.f, 1.f);
            Color c = mix(top, bottom, t);
            uint8_t* p = &img.px[((size_t)y * kW + x) * 4];
            p[0] = (uint8_t)(c.r * 255);
            p[1] = (uint8_t)(c.g * 255);
            p[2] = (uint8_t)(c.b * 255);
            p[3] = 255;
        }
    }
    roundCorners(img, kRadius);
    return fixed_[key] = gfx.texture(img);
}

SDL_Texture* ArtCache::scrim() {
    auto it = fixed_.find("scrim");
    if (it != fixed_.end()) return it->second;
    Image img;
    img.w = kW;
    img.h = kH;
    img.px.resize((size_t)kW * kH * 4);
    for (int y = 0; y < kH; ++y) {
        float t = (float)y / (kH - 1);
        float a = 0.25f + 0.65f * t * t;
        for (int x = 0; x < kW; ++x) img.px[((size_t)y * kW + x) * 4 + 3] = (uint8_t)(a * 255);
    }
    roundCorners(img, kRadius);
    return fixed_["scrim"] = gfx.texture(img);
}

SDL_Texture* ArtCache::compose(const Entry& e, const Category* c, SDL_Texture* icon, int iconW, int iconH) {
    uint64_t t0 = SDL_GetTicksNS();
    SDL_Texture* t = gfx.target(kW, kH);
    if (!t) return nullptr;
    SDL_Texture* prev = SDL_GetRenderTarget(gfx.renderer);
    gfx.setTarget(t);

    uint32_t color = e.color ? e.color : c ? c->color : colorFromName(e.category);
    std::string image = !e.fallbackArt.empty() ? e.fallbackArt : c ? c->image : "";
    SDL_Texture* bg = icon || image.empty() ? nullptr : categoryImage(image);
    SDL_FRect full{0, 0, (float)kW, (float)kH};

    if (bg) {
        gfx.image(bg, full);
        gfx.image(scrim(), full);
    } else {
        gfx.image(gradientCard(color), full);
    }

    float pad = 16;
    if (icon) {
        float cy = (kH - 46) / 2.f;
        SDL_FRect dst{std::round((kW - iconW) / 2.f), std::round(cy - iconH / 2.f + 4), (float)iconW, (float)iconH};
        gfx.image(icon, dst);
        gfx.text(Font::Bold, e.title, kW / 2.f, kH - pad - gfx.lineHeight(Font::Bold), theme::text, kW - 2 * pad, Align::Center);
    } else {
        if (!bg) gfx.icon(c ? c->glyph : "gamepad-2", kW - 32 - pad, pad, 32, fade(theme::text, 0.35f));
        auto lines = gfx.wrap(Font::Title, e.title, kW - 2 * pad, 3);
        float lh = gfx.lineHeight(Font::Title);
        float y = kH - pad - lines.size() * lh;
        for (const auto& l : lines) {
            gfx.text(Font::Title, l, pad, y, theme::text);
            y += lh;
        }
        if (!e.tags.empty())
            gfx.text(Font::Small, e.tags, pad, kH - pad - lines.size() * lh - gfx.lineHeight(Font::Small) - 2,
                     fade(theme::text, 0.7f), kW - 2 * pad);
    }

    gfx.setTarget(prev);
    composeMs += (SDL_GetTicksNS() - t0) / 1e6;
    return t;
}

SDL_Texture* ArtCache::composePoster(const Entry& e, SDL_Texture* poster) {
    uint64_t t0 = SDL_GetTicksNS();
    SDL_Texture* t = gfx.target(kW, kH);
    if (!t) return nullptr;
    SDL_Texture* prev = SDL_GetRenderTarget(gfx.renderer);
    gfx.setTarget(t);
    gfx.image(poster, {0, 0, (float)kW, (float)kH});

    float x = kPosterX + kPosterW + 14, w = kW - x - 14;
    auto lines = gfx.wrap(Font::Bold, e.title, w, 4);
    float lh = gfx.lineHeight(Font::Bold), th = gfx.lineHeight(Font::Small);
    float h = lines.size() * lh + (e.tags.empty() ? 0 : th + 4);
    float y = std::round((kH - h) / 2);
    for (const auto& l : lines) {
        gfx.text(Font::Bold, l, x, y, theme::text);
        y += lh;
    }
    if (!e.tags.empty()) gfx.text(Font::Small, e.tags, x, y + 4, fade(theme::text, 0.7f), w);

    gfx.setTarget(prev);
    composeMs += (SDL_GetTicksNS() - t0) / 1e6;
    return t;
}
