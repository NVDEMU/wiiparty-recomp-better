#pragma once
#include <cstdio>
#include "wp/options.h"

namespace wp::performance_overlay {
inline unsigned char glyph_row(char c, int row) {
    switch (c) {
    case '0': { const unsigned char v[] = {7,5,5,5,7}; return v[row]; }
    case '1': { const unsigned char v[] = {2,6,2,2,7}; return v[row]; }
    case '2': { const unsigned char v[] = {7,1,7,4,7}; return v[row]; }
    case '3': { const unsigned char v[] = {7,1,7,1,7}; return v[row]; }
    case '4': { const unsigned char v[] = {5,5,7,1,1}; return v[row]; }
    case '5': { const unsigned char v[] = {7,4,7,1,7}; return v[row]; }
    case '6': { const unsigned char v[] = {7,4,7,5,7}; return v[row]; }
    case '7': { const unsigned char v[] = {7,1,2,2,2}; return v[row]; }
    case '8': { const unsigned char v[] = {7,5,7,5,7}; return v[row]; }
    case '9': { const unsigned char v[] = {7,5,7,1,7}; return v[row]; }
    case 'F': { const unsigned char v[] = {7,4,6,4,4}; return v[row]; }
    case 'P': { const unsigned char v[] = {6,5,6,4,4}; return v[row]; }
    case 'S': { const unsigned char v[] = {7,4,7,1,7}; return v[row]; }
    case 'M': { const unsigned char v[] = {5,7,7,5,5}; return v[row]; }
    case '.': { const unsigned char v[] = {0,0,0,0,2}; return v[row]; }
    case ':': { const unsigned char v[] = {0,2,0,2,0}; return v[row]; }
    case '|': { const unsigned char v[] = {2,2,2,2,2}; return v[row]; }
    case ' ': return 0;
    default: return 0;
    }
}
inline options::Image make(double fps, double frame_ms) {
    char label[64] = {};
    std::snprintf(label, sizeof(label), "FPS %.0f | %.1f ms", fps, frame_ms);
    options::Image image;
    image.width = 192;
    image.height = 32;
    image.pixels.assign(static_cast<size_t>(image.width) * image.height, 0xD018191Du);
    for (uint32_t y = 3; y + 3 < image.height; ++y) {
        for (uint32_t x = 0; x < 3; ++x) image.pixels[y * image.width + x] = 0xFFE52A78u;
    }
    int x = 11;
    for (const char* ch = label; *ch; ++ch) {
        char c = *ch;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        for (int y = 0; y < 5; ++y) {
            unsigned char pattern = glyph_row(c, y);
            for (int col = 0; col < 3; ++col) {
                if ((pattern & (1u << (2 - col))) == 0) continue;
                for (int sy = 0; sy < 2; ++sy) {
                    for (int sx = 0; sx < 2; ++sx) {
                        int px = x + col * 2 + sx, py = 11 + y * 2 + sy;
                        if (px >= 0 && py >= 0 && px < static_cast<int>(image.width) && py < static_cast<int>(image.height)) {
                            image.pixels[static_cast<size_t>(py) * image.width + static_cast<size_t>(px)] = 0xFFF4F5F7u;
                        }
                    }
                }
            }
        }
        x += 8;
        if (x > static_cast<int>(image.width) - 8) break;
    }
    return image;
}
}
