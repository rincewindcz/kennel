#pragma once

#include <cstdint>
#include <string>
#include <vector>

// RGBA8 image, straight alpha.
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;

    bool empty() const { return w == 0 || h == 0; }
};

bool loadImage(const std::string& path, Image& out);
bool loadImageMemory(const std::string& bytes, Image& out);
bool loadQoi(const std::string& path, Image& out);
bool saveQoi(const std::string& path, const Image& img);

Image resize(const Image& src, int w, int h);
Image cover(const Image& src, int w, int h);
Image contain(const Image& src, int maxW, int maxH, float maxScale);
void roundCorners(Image& img, float radius);
void blur(Image& img, int radius, int passes);
void darken(Image& img, float factor);
void composite(Image& dst, const Image& src, int x, int y);  // src over dst, straight alpha
