#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb_image.h"

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "qoi.h"

#include "util.h"

namespace {

std::string readHead(const std::string& path, size_t max) {
    std::string out(max, '\0');
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    out.resize(fread(out.data(), 1, max, f));
    fclose(f);
    return out;
}

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

bool qoiHeader(const std::string& head, int& w, int& h) {
    if (head.size() < 14 || head.compare(0, 4, "qoif") != 0) return false;
    auto* p = (const uint8_t*)head.data();
    w = (int)be32(p + 4);
    h = (int)be32(p + 8);
    return w > 0 && h > 0;
}

}  // namespace

bool imageInfo(const std::string& path, int& w, int& h) {
    int n;
    if (qoiHeader(readHead(path, 14), w, h)) return true;
    return stbi_info(path.c_str(), &w, &h, &n) != 0;
}

bool loadImage(const std::string& path, Image& out) {
    int w, h, n;
    // Decoding needs w * h * 4 bytes; the device has under 500 MB, so very large images are skipped.
    if (!imageInfo(path, w, h) || (long long)w * h > kMaxImagePixels) return false;
    if (qoiHeader(readHead(path, 14), w, h)) return loadQoi(path, out);
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) return false;
    out.w = w;
    out.h = h;
    out.px.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    return true;
}

bool loadImageMemory(const std::string& bytes, Image& out) {
    int w, h, n;
    auto* p = (const stbi_uc*)bytes.data();
    int len = (int)bytes.size();
    if (!stbi_info_from_memory(p, len, &w, &h, &n) || (long long)w * h > kMaxImagePixels) return false;
    stbi_uc* data = stbi_load_from_memory(p, len, &w, &h, &n, 4);
    if (!data) return false;
    out.w = w;
    out.h = h;
    out.px.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    return true;
}

bool loadQoi(const std::string& path, Image& out) {
    auto bytes = readFile(path);
    if (!bytes) return false;
    qoi_desc desc;
    void* data = qoi_decode(bytes->data(), (int)bytes->size(), &desc, 4);
    if (!data) return false;
    out.w = (int)desc.width;
    out.h = (int)desc.height;
    out.px.assign((uint8_t*)data, (uint8_t*)data + (size_t)out.w * out.h * 4);
    free(data);
    return true;
}

int imageOrientation(const std::string& path) {
    std::string head = readHead(path, 65536);
    auto* p = (const uint8_t*)head.data();
    size_t n = head.size();
    if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return 1;
    size_t pos = 2;
    while (pos + 4 <= n && p[pos] == 0xFF) {
        uint8_t marker = p[pos + 1];
        size_t len = (size_t)p[pos + 2] << 8 | p[pos + 3];
        if (marker == 0xDA || len < 2) break;
        size_t seg = pos + 4, end = std::min(n, pos + 2 + len);
        if (marker == 0xE1 && end - seg >= 14 && memcmp(p + seg, "Exif\0\0", 6) == 0) {
            const uint8_t* t = p + seg + 6;
            size_t tn = end - seg - 6;
            bool le = t[0] == 'I' && t[1] == 'I';
            if (!le && !(t[0] == 'M' && t[1] == 'M')) return 1;
            auto u16 = [&](size_t o) { return le ? (uint32_t)(t[o] | t[o + 1] << 8) : (uint32_t)(t[o] << 8 | t[o + 1]); };
            auto u32 = [&](size_t o) { return le ? u16(o) | u16(o + 2) << 16 : u16(o) << 16 | u16(o + 2); };
            size_t ifd = u32(4);
            if (ifd + 2 > tn) return 1;
            uint32_t count = u16(ifd);
            for (uint32_t i = 0; i < count && ifd + 2 + (i + 1) * 12 <= tn; ++i) {
                size_t e = ifd + 2 + i * 12;
                if (u16(e) != 0x0112) continue;
                uint32_t v = u16(e + 8);
                return v >= 1 && v <= 8 ? (int)v : 1;
            }
            return 1;
        }
        pos += 2 + len;
    }
    return 1;
}

void orient(Image& img, int orientation) {
    if (orientation <= 1 || orientation > 8 || img.empty()) return;
    bool swap = orientation >= 5;
    int w = img.w, h = img.h, ow = swap ? h : w, oh = swap ? w : h;
    std::vector<uint8_t> px((size_t)ow * oh * 4);
    for (int y = 0; y < oh; ++y)
        for (int x = 0; x < ow; ++x) {
            int sx, sy;
            switch (orientation) {
                case 2: sx = w - 1 - x, sy = y; break;
                case 3: sx = w - 1 - x, sy = h - 1 - y; break;
                case 4: sx = x, sy = h - 1 - y; break;
                case 5: sx = y, sy = x; break;
                case 6: sx = y, sy = h - 1 - x; break;
                case 7: sx = w - 1 - y, sy = h - 1 - x; break;
                default: sx = w - 1 - y, sy = x; break;
            }
            memcpy(&px[((size_t)y * ow + x) * 4], &img.px[((size_t)sy * w + sx) * 4], 4);
        }
    img.w = ow;
    img.h = oh;
    img.px = std::move(px);
}

bool saveQoi(const std::string& path, const Image& img) {
    qoi_desc desc{(unsigned)img.w, (unsigned)img.h, 4, QOI_SRGB};
    int len = 0;
    void* data = qoi_encode(img.px.data(), &desc, &len);
    if (!data) return false;
    bool ok = writeFileAtomic(path, std::string_view((const char*)data, len));
    free(data);
    return ok;
}

namespace {

void storePixel(uint8_t* dst, const float* acc, float weight) {
    float a = acc[3] / weight;
    if (a <= 0.f) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
        return;
    }
    for (int c = 0; c < 3; ++c) dst[c] = (uint8_t)std::clamp(std::lround(acc[c] / weight / a), 0L, 255L);
    dst[3] = (uint8_t)std::clamp(std::lround(a * 255.f), 0L, 255L);
}

// Adds a source pixel premultiplied by its alpha, so averaging transparent pixels does not darken edges.
inline void accumulate(float* acc, const uint8_t* p, float weight) {
    float a = p[3] / 255.f * weight;
    acc[0] += p[0] * a;
    acc[1] += p[1] * a;
    acc[2] += p[2] * a;
    acc[3] += a;
}

// Resamples the region (rx, ry, rw, rh) of src to w x h: area average when shrinking, bilinear otherwise.
Image resample(const Image& src, int rx, int ry, int rw, int rh, int w, int h) {
    Image out;
    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * h * 4);
    if (src.empty() || rw <= 0 || rh <= 0 || w <= 0 || h <= 0) return out;
    float sx = (float)rw / w, sy = (float)rh / h;
    auto px = [&](int x, int y) { return &src.px[((size_t)(ry + y) * src.w + rx + x) * 4]; };

    if (sx >= 1.f && sy >= 1.f) {
        for (int y = 0; y < h; ++y) {
            float y0 = y * sy, y1 = y0 + sy;
            for (int x = 0; x < w; ++x) {
                float x0 = x * sx, x1 = x0 + sx;
                float acc[4] = {0, 0, 0, 0}, wsum = 0;
                for (int yy = (int)y0; yy < (int)std::ceil(y1) && yy < rh; ++yy) {
                    float wy = std::min(y1, yy + 1.f) - std::max(y0, (float)yy);
                    for (int xx = (int)x0; xx < (int)std::ceil(x1) && xx < rw; ++xx) {
                        float wgt = wy * (std::min(x1, xx + 1.f) - std::max(x0, (float)xx));
                        accumulate(acc, px(xx, yy), wgt);
                        wsum += wgt;
                    }
                }
                storePixel(&out.px[((size_t)y * w + x) * 4], acc, wsum > 0 ? wsum : 1);
            }
        }
        return out;
    }

    for (int y = 0; y < h; ++y) {
        float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.f, (float)rh - 1);
        int y0 = (int)fy, y1 = std::min(y0 + 1, rh - 1);
        float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.f, (float)rw - 1);
            int x0 = (int)fx, x1 = std::min(x0 + 1, rw - 1);
            float tx = fx - x0;
            float acc[4] = {0, 0, 0, 0};
            accumulate(acc, px(x0, y0), (1 - tx) * (1 - ty));
            accumulate(acc, px(x1, y0), tx * (1 - ty));
            accumulate(acc, px(x0, y1), (1 - tx) * ty);
            accumulate(acc, px(x1, y1), tx * ty);
            storePixel(&out.px[((size_t)y * w + x) * 4], acc, 1);
        }
    }
    return out;
}

}  // namespace

Image resize(const Image& src, int w, int h) { return resample(src, 0, 0, src.w, src.h, w, h); }

Image cover(const Image& src, int w, int h) {
    if (src.empty()) return {};
    float scale = std::max((float)w / src.w, (float)h / src.h);
    int cw = std::min(src.w, (int)std::lround(w / scale));
    int ch = std::min(src.h, (int)std::lround(h / scale));
    return resample(src, (src.w - cw) / 2, (src.h - ch) / 2, cw, ch, w, h);
}

Image contain(const Image& src, int maxW, int maxH, float maxScale) {
    if (src.empty()) return {};
    float scale = std::min({(float)maxW / src.w, (float)maxH / src.h, maxScale});
    int w = std::max(1, (int)std::lround(src.w * scale)), h = std::max(1, (int)std::lround(src.h * scale));
    if (w == src.w && h == src.h) return src;
    return resize(src, w, h);
}

void roundCorners(Image& img, float r) {
    for (int y = 0; y < img.h; ++y) {
        for (int x = 0; x < img.w; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float qx = std::max(std::max(r - px, px - (img.w - r)), 0.f);
            float qy = std::max(std::max(r - py, py - (img.h - r)), 0.f);
            if (qx == 0.f || qy == 0.f) continue;
            float d = std::sqrt(qx * qx + qy * qy) - r;
            float cov = std::clamp(0.5f - d, 0.f, 1.f);
            uint8_t& a = img.px[((size_t)y * img.w + x) * 4 + 3];
            a = (uint8_t)std::lround(a * cov);
        }
    }
}

void blur(Image& img, int radius, int passes) {
    if (img.empty() || radius <= 0) return;
    std::vector<uint8_t> tmp(img.px.size());
    auto pass = [&](const uint8_t* in, uint8_t* out, int w, int h, int stride, int step) {
        // 1D box blur along rows (step 4) or columns (step stride) with clamped edges.
        int len = step == 4 ? w : h, lines = step == 4 ? h : w;
        int lineStep = step == 4 ? stride : 4;
        for (int l = 0; l < lines; ++l) {
            const uint8_t* src = in + (size_t)l * lineStep;
            uint8_t* dst = out + (size_t)l * lineStep;
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int i = -radius; i <= radius; ++i) sum += src[std::clamp(i, 0, len - 1) * step + c];
                for (int i = 0; i < len; ++i) {
                    dst[i * step + c] = (uint8_t)(sum / (2 * radius + 1));
                    sum += src[std::min(i + radius + 1, len - 1) * step + c];
                    sum -= src[std::max(i - radius, 0) * step + c];
                }
            }
        }
    };
    int stride = img.w * 4;
    for (int p = 0; p < passes; ++p) {
        pass(img.px.data(), tmp.data(), img.w, img.h, stride, 4);
        pass(tmp.data(), img.px.data(), img.w, img.h, stride, stride);
    }
}

void darken(Image& img, float factor) {
    for (size_t i = 0; i < img.px.size(); i += 4)
        for (int c = 0; c < 3; ++c) img.px[i + c] = (uint8_t)std::lround(img.px[i + c] * factor);
}

void composite(Image& dst, const Image& src, int x, int y) {
    for (int sy = 0; sy < src.h; ++sy) {
        int dy = y + sy;
        if (dy < 0 || dy >= dst.h) continue;
        for (int sx = 0; sx < src.w; ++sx) {
            int dx = x + sx;
            if (dx < 0 || dx >= dst.w) continue;
            const uint8_t* s = &src.px[((size_t)sy * src.w + sx) * 4];
            uint8_t* d = &dst.px[((size_t)dy * dst.w + dx) * 4];
            float a = s[3] / 255.f, da = d[3] / 255.f;
            float oa = a + da * (1 - a);
            if (oa <= 0) continue;
            for (int c = 0; c < 3; ++c) d[c] = (uint8_t)std::lround((s[c] * a + d[c] * da * (1 - a)) / oa);
            d[3] = (uint8_t)std::lround(oa * 255);
        }
    }
}
