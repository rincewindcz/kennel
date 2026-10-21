#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "qoi.h"

#include "util.h"

namespace {
constexpr long long kMaxPixels = 16'000'000;
}

bool loadImage(const std::string& path, Image& out) {
    int w, h, n;
    // Decoding needs w * h * 4 bytes; the device has under 500 MB, so very large images are skipped.
    if (!stbi_info(path.c_str(), &w, &h, &n) || (long long)w * h > kMaxPixels) return false;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
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
