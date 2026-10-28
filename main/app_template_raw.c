/* Előre dekódolt sablonképek előállítása és ellenőrzése; lásd app_template_raw.h. */
#include "app_template_raw.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"

static uint32_t be32v(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void le32v(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t le32r(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool app_template_raw_name(const char *png_name, unsigned width, unsigned height,
                           char *out, size_t out_size)
{
    size_t length = png_name ? strlen(png_name) : 0;
    if (length < 5 || length > 64 || strcmp(png_name + length - 4, ".png") || !width || !height ||
        width > 1280 || height > 800)
        return false;
    int written = snprintf(out, out_size, "%.*s-%ux%u.raw", (int)(length - 4), png_name, width,
                           height);
    return written > 0 && (size_t)written < out_size;
}

bool app_template_raw_is_name(const char *name, char base[66])
{
    size_t length = name ? strlen(name) : 0;
    if (length < 10 || length > 80 || strcmp(name + length - 4, ".raw"))
        return false;
    /* `<alap>-<w>x<h>.raw`: a mérethatározó a név végén áll. */
    const char *dash = NULL;
    for (size_t i = length - 4; i-- > 0;)
        if (name[i] == '-')
        {
            dash = name + i;
            break;
        }
    if (!dash || dash == name)
        return false;
    unsigned width = 0, height = 0;
    char tail[16];
    size_t tail_length = (size_t)(name + length - 4 - (dash + 1));
    if (tail_length == 0 || tail_length >= sizeof(tail))
        return false;
    memcpy(tail, dash + 1, tail_length);
    tail[tail_length] = 0;
    if (sscanf(tail, "%ux%u", &width, &height) != 2 || !width || !height)
        return false;
    size_t base_length = (size_t)(dash - name);
    if (base_length + 4 >= 66)
        return false;
    for (size_t i = 0; i < base_length; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    if (base)
        snprintf(base, 66, "%.*s.png", (int)base_length, name);
    return true;
}

/* 8 bites, nem interlace RGB/RGBA PNG kibontása RGBA8888-ba. */
static uint8_t *png_decode(const unsigned char *png, size_t size, unsigned *width,
                           unsigned *height)
{
    if (size < 45 || memcmp(png, "\211PNG\r\n\032\n", 8))
        return NULL;
    unsigned char *compressed = malloc(size);
    if (!compressed)
        return NULL;
    size_t compressed_size = 0, pos = 8;
    unsigned channels = 0, w = 0, h = 0;
    bool ok = false;
    while (pos + 12 <= size)
    {
        uint32_t n = be32v(png + pos);
        const unsigned char *type = png + pos + 4, *data = png + pos + 8;
        if (n > size - pos - 12)
            break;
        if (pos == 8)
        {
            if (n != 13 || memcmp(type, "IHDR", 4))
                break;
            w = be32v(data);
            h = be32v(data + 4);
            if (!w || !h || w > 1280 || h > 800 || data[8] != 8 ||
                (data[9] != 2 && data[9] != 6) || data[10] || data[11] || data[12])
                break;
            channels = data[9] == 6 ? 4 : 3;
        }
        else if (!memcmp(type, "IDAT", 4))
        {
            memcpy(compressed + compressed_size, data, n);
            compressed_size += n;
        }
        else if (!memcmp(type, "IEND", 4))
        {
            ok = n == 0;
            break;
        }
        pos += 12 + n;
    }
    if (!ok || !channels || !compressed_size)
    {
        free(compressed);
        return NULL;
    }
    size_t stride = (size_t)w * channels + 1;
    uLongf expected = (uLongf)(stride * h), actual = expected;
    unsigned char *filtered = malloc(expected);
    if (!filtered)
    {
        free(compressed);
        return NULL;
    }
    ok = uncompress(filtered, &actual, compressed, (uLong)compressed_size) == Z_OK &&
         actual == expected;
    free(compressed);
    uint8_t *rgba = ok ? malloc((size_t)w * h * 4) : NULL;
    if (!rgba)
    {
        free(filtered);
        return NULL;
    }
    /* PNG sorszűrők visszafejtése (None, Sub, Up, Average, Paeth). */
    unsigned char *previous = NULL, *current = NULL;
    size_t row_bytes = (size_t)w * channels;
    for (unsigned y = 0; y < h && ok; y++)
    {
        unsigned char filter = filtered[y * stride];
        current = filtered + y * stride + 1;
        if (filter > 4)
        {
            ok = false;
            break;
        }
        for (size_t x = 0; x < row_bytes; x++)
        {
            unsigned left = x >= channels ? current[x - channels] : 0;
            unsigned up = previous ? previous[x] : 0;
            unsigned up_left = (previous && x >= channels) ? previous[x - channels] : 0;
            unsigned value = current[x];
            switch (filter)
            {
            case 1:
                value += left;
                break;
            case 2:
                value += up;
                break;
            case 3:
                value += (left + up) / 2;
                break;
            case 4:
            {
                int p = (int)left + (int)up - (int)up_left;
                int pa = abs(p - (int)left), pb = abs(p - (int)up), pc = abs(p - (int)up_left);
                value += (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : up_left);
                break;
            }
            default:
                break;
            }
            current[x] = (unsigned char)value;
        }
        uint8_t *target = rgba + (size_t)y * w * 4;
        for (unsigned x = 0; x < w; x++)
        {
            const unsigned char *source = current + (size_t)x * channels;
            target[x * 4] = source[0];
            target[x * 4 + 1] = source[1];
            target[x * 4 + 2] = source[2];
            target[x * 4 + 3] = channels == 4 ? source[3] : 255;
        }
        previous = current;
    }
    free(filtered);
    if (!ok)
    {
        free(rgba);
        return NULL;
    }
    *width = w;
    *height = h;
    return rgba;
}

/* Bilineáris átméretezés; azonos méretnél érintetlenül másol. */
static uint8_t *scale_rgba(const uint8_t *source, unsigned sw, unsigned sh, unsigned dw,
                           unsigned dh)
{
    uint8_t *out = malloc((size_t)dw * dh * 4);
    if (!out)
        return NULL;
    if (sw == dw && sh == dh)
    {
        memcpy(out, source, (size_t)dw * dh * 4);
        return out;
    }
    for (unsigned y = 0; y < dh; y++)
    {
        /* Képpontközép-alapú leképezés: a széleken nem csúszik el a kép. */
        double sy = ((double)y + 0.5) * (double)sh / (double)dh - 0.5;
        if (sy < 0)
            sy = 0;
        unsigned y0 = (unsigned)sy;
        if (y0 >= sh - 1)
            y0 = sh > 1 ? sh - 2 : 0;
        double fy = sh > 1 ? sy - (double)y0 : 0;
        unsigned y1 = sh > 1 ? y0 + 1 : y0;
        for (unsigned x = 0; x < dw; x++)
        {
            double sx = ((double)x + 0.5) * (double)sw / (double)dw - 0.5;
            if (sx < 0)
                sx = 0;
            unsigned x0 = (unsigned)sx;
            if (x0 >= sw - 1)
                x0 = sw > 1 ? sw - 2 : 0;
            double fx = sw > 1 ? sx - (double)x0 : 0;
            unsigned x1 = sw > 1 ? x0 + 1 : x0;
            const uint8_t *p00 = source + ((size_t)y0 * sw + x0) * 4;
            const uint8_t *p01 = source + ((size_t)y0 * sw + x1) * 4;
            const uint8_t *p10 = source + ((size_t)y1 * sw + x0) * 4;
            const uint8_t *p11 = source + ((size_t)y1 * sw + x1) * 4;
            uint8_t *target = out + ((size_t)y * dw + x) * 4;
            for (unsigned c = 0; c < 4; c++)
            {
                double top = p00[c] + ((double)p01[c] - p00[c]) * fx;
                double bottom = p10[c] + ((double)p11[c] - p10[c]) * fx;
                double value = top + (bottom - top) * fy;
                target[c] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value + 0.5);
            }
        }
    }
    return out;
}

bool app_template_raw_build(const unsigned char *png, size_t png_size, unsigned width,
                            unsigned height, uint8_t **out, size_t *out_size)
{
    if (!png || !out || !out_size || !width || !height || width > 1280 || height > 800)
        return false;
    *out = NULL;
    *out_size = 0;
    unsigned sw = 0, sh = 0;
    uint8_t *rgba = png_decode(png, png_size, &sw, &sh);
    if (!rgba)
        return false;
    /* Azonos méretnél (hátterek) nincs másolat: a csúcsmemória 4 MiB-tal kevesebb. */
    uint8_t *scaled = (sw == width && sh == height) ? rgba : scale_rgba(rgba, sw, sh, width, height);
    if (scaled != rgba)
        free(rgba);
    if (!scaled)
        return false;

    bool opaque = true;
    for (size_t i = 3; opaque && i < (size_t)width * height * 4; i += 4)
        if (scaled[i] != 255)
            opaque = false;
    unsigned format = opaque ? APP_TEMPLATE_RAW_RGB565 : APP_TEMPLATE_RAW_RGB565A8;
    size_t stride = (size_t)width * 2;
    size_t pixels = stride * height + (opaque ? 0 : (size_t)width * height);
    size_t total = APP_TEMPLATE_RAW_HEADER + pixels;
    if (total > APP_TEMPLATE_RAW_MAX)
    {
        free(scaled);
        return false;
    }
    uint8_t *buffer = malloc(total);
    if (!buffer)
    {
        free(scaled);
        return false;
    }
    uint8_t *color = buffer + APP_TEMPLATE_RAW_HEADER;
    uint8_t *alpha = color + stride * height;
    for (size_t i = 0; i < (size_t)width * height; i++)
    {
        unsigned r = scaled[i * 4], g = scaled[i * 4 + 1], b = scaled[i * 4 + 2];
        uint16_t value = (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
        color[i * 2] = (uint8_t)value;
        color[i * 2 + 1] = (uint8_t)(value >> 8);
        if (!opaque)
            alpha[i] = scaled[i * 4 + 3];
    }
    free(scaled);

    memset(buffer, 0, APP_TEMPLATE_RAW_HEADER);
    le32v(buffer, APP_TEMPLATE_RAW_MAGIC);
    le32v(buffer + 4, format);
    le32v(buffer + 8, width);
    le32v(buffer + 12, height);
    le32v(buffer + 16, (uint32_t)stride);
    le32v(buffer + 20, (uint32_t)pixels);
    le32v(buffer + 24, (uint32_t)png_size);
    le32v(buffer + 28, (uint32_t)crc32(0, buffer + APP_TEMPLATE_RAW_HEADER, (uInt)pixels));
    *out = buffer;
    *out_size = total;
    return true;
}

bool app_template_raw_parse(const uint8_t *data, size_t size, unsigned width, unsigned height,
                            uint32_t source_size, app_template_raw_t *out)
{
    if (!data || !out || size <= APP_TEMPLATE_RAW_HEADER || size > APP_TEMPLATE_RAW_MAX)
        return false;
    app_template_raw_t info = {
        .format = le32r(data + 4),
        .width = le32r(data + 8),
        .height = le32r(data + 12),
        .stride = le32r(data + 16),
        .pixels_size = le32r(data + 20),
        .source_size = le32r(data + 24),
        .data_crc = le32r(data + 28),
    };
    if (le32r(data) != APP_TEMPLATE_RAW_MAGIC || info.width != width || info.height != height ||
        info.stride != width * 2U || !width || !height)
        return false;
    size_t expected = (size_t)info.stride * info.height;
    if (info.format == APP_TEMPLATE_RAW_RGB565A8)
        expected += (size_t)info.width * info.height;
    else if (info.format != APP_TEMPLATE_RAW_RGB565)
        return false;
    if (info.pixels_size != expected || size != APP_TEMPLATE_RAW_HEADER + expected)
        return false;
    /* A csomag cseréjekor a régi segédfájl nem használható, a sérült pedig
     * ugyanúgy visszaesik a Rendszer nézetre, mint a hibás PNG. */
    if (info.source_size != source_size ||
        info.data_crc != (uint32_t)crc32(0, data + APP_TEMPLATE_RAW_HEADER, (uInt)expected))
        return false;
    info.pixels = data + APP_TEMPLATE_RAW_HEADER;
    *out = info;
    return true;
}
