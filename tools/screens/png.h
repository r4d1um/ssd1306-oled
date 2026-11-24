// SPDX-License-Identifier: MIT
// Minimal PNG writer (8-bit RGB, uncompressed deflate blocks) - no zlib needed.
#ifndef SCREENS_PNG_H
#define SCREENS_PNG_H

#include <cstdint>
#include <cstdio>
#include <vector>

struct rgb_image {
    int w, h;
    std::vector<uint8_t> px; // w*h*3
    rgb_image(int width, int height, uint32_t fill = 0)
        : w(width), h(height), px((size_t)width * height * 3)
    {
        for (int i = 0; i < w * h; i++)
            set_i(i, fill);
    }
    void set_i(int i, uint32_t c)
    {
        px[(size_t)i * 3] = (uint8_t)(c >> 16);
        px[(size_t)i * 3 + 1] = (uint8_t)(c >> 8);
        px[(size_t)i * 3 + 2] = (uint8_t)c;
    }
    void set(int x, int y, uint32_t c)
    {
        if (x >= 0 && y >= 0 && x < w && y < h)
            set_i(y * w + x, c);
    }
    void rect(int x, int y, int rw, int rh, uint32_t c)
    {
        for (int j = y; j < y + rh; j++)
            for (int i = x; i < x + rw; i++)
                set(i, j, c);
    }
};

static uint32_t png_crc(const uint8_t *p, size_t n, uint32_t c = 0xFFFFFFFFu)
{
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c;
}

static bool write_png(const char *path, const rgb_image &img)
{
    std::vector<uint8_t> raw;
    for (int y = 0; y < img.h; y++) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), img.px.begin() + (size_t)y * img.w * 3, img.px.begin() + (size_t)(y + 1) * img.w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < raw.size() || off == 0; off += 65535) {
        size_t n = raw.size() - off < 65535 ? raw.size() - off : 65535;
        z.push_back(off + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)n);
        z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)~n);
        z.push_back((uint8_t)(~n >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        if (n == 0)
            break;
    }
    uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8)
        z.push_back((uint8_t)(adler >> s));

    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    auto u32 = [&](uint32_t v) { uint8_t q[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(q, 1, 4, f); };
    auto chunk = [&](const char *type, const std::vector<uint8_t> &data) {
        u32((uint32_t)data.size());
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        fwrite(td.data(), 1, td.size(), f);
        u32(png_crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
    };
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = {(uint8_t)(img.w >> 24), (uint8_t)(img.w >> 16), (uint8_t)(img.w >> 8), (uint8_t)img.w,
                                 (uint8_t)(img.h >> 24), (uint8_t)(img.h >> 16), (uint8_t)(img.h >> 8), (uint8_t)img.h,
                                 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    return fclose(f) == 0;
}

#endif
