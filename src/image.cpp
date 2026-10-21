#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO_FAILURE_STRINGS
#include "stb_image.h"

#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "qoi.h"

#include "util.h"

bool loadImage(const std::string& path, Image& out) {
    int w, h, n;
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

// Premultiplied float copy, so averaging transparent pixels does not darken edges.
std::vector<float> premultiply(const Image& img) {
    std::vector<float> out((size_t)img.w * img.h * 4);
    for (size_t i = 0; i < (size_t)img.w * img.h; ++i) {
        float a = img.px[i * 4 + 3] / 255.f;
        out[i * 4 + 0] = img.px[i * 4 + 0] * a;
        out[i * 4 + 1] = img.px[i * 4 + 1] * a;
        out[i * 4 + 2] = img.px[i * 4 + 2] * a;
        out[i * 4 + 3] = a;
    }
    return out;
}

void storePixel(uint8_t* dst, const float* acc, float weight) {
    float a = acc[3] / weight;
    if (a <= 0.f) {
        dst[0] = dst[1] = dst[2] = dst[3] = 0;
        return;
    }
    for (int c = 0; c < 3; ++c) dst[c] = (uint8_t)std::clamp(std::lround(acc[c] / weight / a), 0L, 255L);
    dst[3] = (uint8_t)std::clamp(std::lround(a * 255.f), 0L, 255L);
}

}  // namespace

Image resize(const Image& src, int w, int h) {
    Image out;
    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * h * 4);
    if (src.empty() || w <= 0 || h <= 0) return out;
    std::vector<float> pm = premultiply(src);
    float sx = (float)src.w / w, sy = (float)src.h / h;

    if (sx >= 1.f && sy >= 1.f) {
        // Area average over the source box of every destination pixel.
        for (int y = 0; y < h; ++y) {
            float y0 = y * sy, y1 = y0 + sy;
            for (int x = 0; x < w; ++x) {
                float x0 = x * sx, x1 = x0 + sx;
                float acc[4] = {0, 0, 0, 0}, wsum = 0;
                for (int yy = (int)y0; yy < (int)std::ceil(y1) && yy < src.h; ++yy) {
                    float wy = std::min(y1, yy + 1.f) - std::max(y0, (float)yy);
                    for (int xx = (int)x0; xx < (int)std::ceil(x1) && xx < src.w; ++xx) {
                        float wgt = wy * (std::min(x1, xx + 1.f) - std::max(x0, (float)xx));
                        const float* p = &pm[((size_t)yy * src.w + xx) * 4];
                        for (int c = 0; c < 4; ++c) acc[c] += p[c] * wgt;
                        wsum += wgt;
                    }
                }
                storePixel(&out.px[((size_t)y * w + x) * 4], acc, wsum > 0 ? wsum : 1);
            }
        }
        return out;
    }

    // Bilinear for upscaling (or mixed scaling).
    for (int y = 0; y < h; ++y) {
        float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.f, (float)src.h - 1);
        int y0 = (int)fy, y1 = std::min(y0 + 1, src.h - 1);
        float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.f, (float)src.w - 1);
            int x0 = (int)fx, x1 = std::min(x0 + 1, src.w - 1);
            float tx = fx - x0;
            float acc[4];
            for (int c = 0; c < 4; ++c) {
                float a = pm[((size_t)y0 * src.w + x0) * 4 + c], b = pm[((size_t)y0 * src.w + x1) * 4 + c];
                float d = pm[((size_t)y1 * src.w + x0) * 4 + c], e = pm[((size_t)y1 * src.w + x1) * 4 + c];
                acc[c] = (a + (b - a) * tx) * (1 - ty) + (d + (e - d) * tx) * ty;
            }
            storePixel(&out.px[((size_t)y * w + x) * 4], acc, 1);
        }
    }
    return out;
}

Image cover(const Image& src, int w, int h) {
    if (src.empty()) return {};
    float scale = std::max((float)w / src.w, (float)h / src.h);
    int cw = std::min(src.w, (int)std::lround(w / scale));
    int ch = std::min(src.h, (int)std::lround(h / scale));
    int cx = (src.w - cw) / 2, cy = (src.h - ch) / 2;
    Image crop;
    crop.w = cw;
    crop.h = ch;
    crop.px.resize((size_t)cw * ch * 4);
    for (int y = 0; y < ch; ++y)
        memcpy(&crop.px[(size_t)y * cw * 4], &src.px[((size_t)(y + cy) * src.w + cx) * 4], (size_t)cw * 4);
    return resize(crop, w, h);
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
