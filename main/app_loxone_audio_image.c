/* Borítók szoftveres dekódolása, LVGL- és JPEG-hardverhívások nélkül.
 * PNG: 1/2/4/8 bites paletta/szürke, 8 bites RGB/RGBA; nem interlace.
 * JPEG: a meglévő LVGL TJpgDec saját, feladatonkénti példánya; ritka
 * mintavételnél egy legfeljebb 512 pixeles, csak JPEG-et kezelő tartalékút. */
#include "app_loxone_audio_media.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"
#include "src/libs/tjpgd/tjpgd.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

static void *audio_image_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(1, size);
#endif
}

static uint32_t audio_be32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

static uint16_t audio_rgb565(unsigned red, unsigned green, unsigned blue, unsigned alpha)
{
    /* Átlátszó rádiólogó a lejátszó sötét kijelzőjére kerül. */
    red = (red * alpha + 32U * (255U - alpha) + 127U) / 255U;
    green = (green * alpha + 52U * (255U - alpha) + 127U) / 255U;
    blue = (blue * alpha + 58U * (255U - alpha) + 127U) / 255U;
    return (uint16_t)((red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3));
}

static lx_audio_image_t *audio_image_create(unsigned width, unsigned height, unsigned size,
                                            unsigned source_max)
{
    if (!width || !height || width > source_max || height > source_max ||
        !size || size > 256)
        return NULL;
    lx_audio_image_t *image = audio_image_alloc(sizeof(*image));
    if (!image)
        return NULL;
    image->width = width >= height ? size : (width * size) / height;
    image->height = height >= width ? size : (height * size) / width;
    if (!image->width)
        image->width = 1;
    if (!image->height)
        image->height = 1;
    image->pixels = audio_image_alloc((size_t)image->width * image->height * sizeof(uint16_t));
    if (!image->pixels)
    {
        free(image);
        return NULL;
    }
    return image;
}

void lx_audio_image_free(lx_audio_image_t *image)
{
    if (image)
    {
        free(image->pixels);
        free(image);
    }
}

static void *audio_zalloc(void *opaque, unsigned items, unsigned size)
{
    (void)opaque;
    return items && size <= SIZE_MAX / items ? audio_image_alloc((size_t)items * size) : NULL;
}

static void audio_zfree(void *opaque, void *address)
{
    (void)opaque;
    free(address);
}

typedef struct
{
    uint8_t palette[256][4];
    uint16_t transparent[3];
    unsigned width, height, depth, color, channels, colors;
    bool transparency, palette_seen, idat_seen, idat_finished;
} audio_png_t;

static unsigned audio_png_sample(const uint8_t *row, unsigned x, unsigned depth)
{
    return depth == 8 ? row[x] :
        (row[(x * depth) / 8] >> (8 - depth - (x * depth) % 8)) & ((1U << depth) - 1U);
}

static unsigned audio_paeth(unsigned left, unsigned up, unsigned corner)
{
    int prediction = (int)left + (int)up - (int)corner;
    int a = abs(prediction - (int)left), b = abs(prediction - (int)up);
    int c = abs(prediction - (int)corner);
    return a <= b && a <= c ? left : b <= c ? up : corner;
}

static lx_audio_image_t *audio_png_decode(const uint8_t *data, size_t length, unsigned size)
{
    audio_png_t *png = audio_image_alloc(sizeof(*png));
    uint8_t *compressed = audio_image_alloc(length);
    if (!png || !compressed)
    {
        free(png);
        free(compressed);
        return NULL;
    }
    for (unsigned i = 0; i < 256; ++i)
        png->palette[i][3] = 255;
    size_t position = 8, compressed_size = 0;
    bool ok = false;
    unsigned chunks = 0;
    while (position + 12 <= length && ++chunks <= 1024)
    {
        unsigned n = audio_be32(data + position);
        const uint8_t *type = data + position + 4, *value = data + position + 8;
        if (n > length - position - 12 ||
            (uint32_t)crc32(0, type, (uInt)n + 4) != audio_be32(value + n))
            break;
        if (position == 8)
        {
            if (n != 13 || memcmp(type, "IHDR", 4))
                break;
            png->width = audio_be32(value);
            png->height = audio_be32(value + 4);
            png->depth = value[8];
            png->color = value[9];
            png->channels = png->color == 0 || png->color == 3 ? 1 :
                png->color == 2 ? 3 : png->color == 4 ? 2 : png->color == 6 ? 4 : 0;
            bool bits = png->depth == 8 || ((png->color == 0 || png->color == 3) &&
                (png->depth == 1 || png->depth == 2 || png->depth == 4));
            if (!png->width || !png->height || png->width > LX_AUDIO_MEDIA_SOURCE_MAX ||
                png->height > LX_AUDIO_MEDIA_SOURCE_MAX || !png->channels || !bits ||
                value[10] || value[11] || value[12])
                break;
        }
        else if (!memcmp(type, "PLTE", 4))
        {
            if (png->idat_seen || png->palette_seen || n == 0 || n > 768 || n % 3)
                break;
            png->colors = n / 3;
            png->palette_seen = true;
            for (unsigned i = 0; i < png->colors; ++i)
                memcpy(png->palette[i], value + i * 3, 3);
        }
        else if (!memcmp(type, "tRNS", 4))
        {
            if (png->idat_seen || png->transparency)
                break;
            png->transparency = true;
            if (png->color == 3 && png->palette_seen && n <= png->colors)
                for (unsigned i = 0; i < n; ++i)
                    png->palette[i][3] = value[i];
            else if ((png->color == 0 && n == 2) || (png->color == 2 && n == 6))
                for (unsigned i = 0; i < n / 2; ++i)
                    png->transparent[i] = (uint16_t)(value[2 * i] << 8 | value[2 * i + 1]);
            else
                break;
        }
        else if (!memcmp(type, "IDAT", 4))
        {
            if (png->idat_finished || (png->color == 3 && !png->palette_seen))
                break;
            memcpy(compressed + compressed_size, value, n);
            compressed_size += n;
            png->idat_seen = true;
        }
        else if (!memcmp(type, "IEND", 4))
        {
            ok = n == 0 && position + 12 == length && png->idat_seen;
            break;
        }
        else
        {
            if (!(type[0] & 0x20))
                break;
            if (png->idat_seen)
                png->idat_finished = true;
        }
        position += n + 12;
    }
    size_t row_bytes = ((size_t)png->width * png->channels * png->depth + 7) / 8;
    size_t filtered_size = (row_bytes + 1) * png->height;
    uint8_t *filtered = ok ? audio_image_alloc(filtered_size) : NULL;
    z_stream stream = {.zalloc = audio_zalloc, .zfree = audio_zfree};
    if (filtered && compressed_size && inflateInit(&stream) == Z_OK)
    {
        stream.next_in = compressed;
        stream.avail_in = (uInt)compressed_size;
        stream.next_out = filtered;
        stream.avail_out = (uInt)filtered_size;
        ok = inflate(&stream, Z_FINISH) == Z_STREAM_END && stream.total_out == filtered_size &&
             stream.total_in == compressed_size;
        inflateEnd(&stream);
    }
    else
        ok = false;
    free(compressed);
    unsigned bpp = (png->channels * png->depth + 7) / 8;
    if (!bpp)
        bpp = 1;
    for (unsigned y = 0; ok && y < png->height; ++y)
    {
        uint8_t *row = filtered + (row_bytes + 1) * y + 1;
        const uint8_t *previous = y ? row - row_bytes - 1 : NULL;
        unsigned filter = row[-1];
        if (filter > 4)
        {
            ok = false;
            break;
        }
        for (size_t x = 0; x < row_bytes; ++x)
        {
            unsigned left = x >= bpp ? row[x - bpp] : 0;
            unsigned up = previous ? previous[x] : 0;
            unsigned corner = previous && x >= bpp ? previous[x - bpp] : 0;
            row[x] = (uint8_t)(row[x] + (filter == 1 ? left : filter == 2 ? up :
                filter == 3 ? (left + up) / 2 : filter == 4 ? audio_paeth(left, up, corner) : 0));
        }
    }
    lx_audio_image_t *image = ok ? audio_image_create(png->width, png->height, size,
                                                     LX_AUDIO_MEDIA_SOURCE_MAX) : NULL;
    for (unsigned y = 0; image && y < image->height; ++y)
    {
        unsigned sy = y * png->height / image->height;
        const uint8_t *row = filtered + (row_bytes + 1) * sy + 1;
        for (unsigned x = 0; x < image->width; ++x)
        {
            unsigned sx = x * png->width / image->width;
            unsigned red, green, blue, alpha = 255;
            if (png->color == 3)
            {
                unsigned index = audio_png_sample(row, sx, png->depth);
                if (index >= png->colors)
                {
                    lx_audio_image_free(image);
                    image = NULL;
                    break;
                }
                red = png->palette[index][0];
                green = png->palette[index][1];
                blue = png->palette[index][2];
                alpha = png->palette[index][3];
            }
            else if (png->color == 0)
            {
                unsigned sample = audio_png_sample(row, sx, png->depth);
                red = green = blue = sample * 255 / ((1U << png->depth) - 1);
                if (png->transparency && sample == png->transparent[0])
                    alpha = 0;
            }
            else if (png->color == 4)
            {
                red = green = blue = row[sx * 2];
                alpha = row[sx * 2 + 1];
            }
            else
            {
                const uint8_t *pixel = row + sx * png->channels;
                red = pixel[0];
                green = pixel[1];
                blue = pixel[2];
                if (png->color == 6)
                    alpha = pixel[3];
                else if (png->transparency && red == png->transparent[0] &&
                         green == png->transparent[1] && blue == png->transparent[2])
                    alpha = 0;
            }
            image->pixels[y * image->width + x] = audio_rgb565(red, green, blue, alpha);
        }
    }
    free(filtered);
    free(png);
    return image;
}

#if LV_USE_TJPGD
/* A tartalékdekóder összes foglalása közös, kemény korlát alá esik.
 * A számláló párhuzamos hívásnál sem engedi a keret túllépését; a fejléc
 * megőrzi a méretet, így a hibautak és a realloc is pontosan felszabadít. */
#define AUDIO_JPEG_WORKING_MAX (4U * 1024U * 1024U)
typedef union
{
#ifdef _MSC_VER
    /* Az MSVC C fejléceiben nincs max_align_t. A Windows-heap alapillesztését
     * a fejléc mérete is őrizze meg: 64 biten 16, 32 biten 8 bájt. */
#ifdef _WIN64
    __declspec(align(16)) unsigned char alignment[16];
#else
    __declspec(align(8)) unsigned char alignment[8];
#endif
#else
    max_align_t alignment;
#endif
    size_t bytes;
} audio_jpeg_allocation_t;

#ifdef _MSC_VER
#include <intrin.h>
static volatile long audio_jpeg_working_bytes;

static bool audio_jpeg_claim(size_t bytes)
{
    long previous = _InterlockedCompareExchange(&audio_jpeg_working_bytes, 0, 0);
    while (bytes <= AUDIO_JPEG_WORKING_MAX - (size_t)previous)
    {
        long observed = _InterlockedCompareExchange(&audio_jpeg_working_bytes,
            previous + (long)bytes, previous);
        if (observed == previous)
            return true;
        previous = observed;
    }
    return false;
}

static void audio_jpeg_unclaim(size_t bytes)
{
    (void)_InterlockedExchangeAdd(&audio_jpeg_working_bytes, -(long)bytes);
}
#else
#include <stdatomic.h>
static atomic_size_t audio_jpeg_working_bytes;

static bool audio_jpeg_claim(size_t bytes)
{
    size_t previous = atomic_load(&audio_jpeg_working_bytes);
    while (bytes <= AUDIO_JPEG_WORKING_MAX - previous)
        if (atomic_compare_exchange_weak(&audio_jpeg_working_bytes, &previous, previous + bytes))
            return true;
    return false;
}

static void audio_jpeg_unclaim(size_t bytes)
{
    atomic_fetch_sub(&audio_jpeg_working_bytes, bytes);
}
#endif

static void *audio_jpeg_alloc(size_t bytes)
{
    if (!bytes || bytes > AUDIO_JPEG_WORKING_MAX - sizeof(audio_jpeg_allocation_t))
        return NULL;
    size_t total = bytes + sizeof(audio_jpeg_allocation_t);
    if (!audio_jpeg_claim(total))
        return NULL;
    audio_jpeg_allocation_t *allocation = audio_image_alloc(total);
    if (!allocation)
    {
        audio_jpeg_unclaim(total);
        return NULL;
    }
    allocation->bytes = total;
    return allocation + 1;
}

static void audio_jpeg_free(void *pointer)
{
    if (!pointer)
        return;
    audio_jpeg_allocation_t *allocation = (audio_jpeg_allocation_t *)pointer - 1;
    size_t bytes = allocation->bytes;
    free(allocation);
    audio_jpeg_unclaim(bytes);
}

static inline void *audio_jpeg_realloc(void *pointer, size_t bytes)
{
    if (!pointer)
        return audio_jpeg_alloc(bytes);
    if (!bytes)
    {
        audio_jpeg_free(pointer);
        return NULL;
    }
    void *next = audio_jpeg_alloc(bytes);
    if (!next)
        return NULL;
    size_t previous = ((audio_jpeg_allocation_t *)pointer - 1)->bytes - sizeof(audio_jpeg_allocation_t);
    memcpy(next, pointer, previous < bytes ? previous : bytes);
    audio_jpeg_free(pointer);
    return next;
}

/* A TuneIn valódi rádiólogója 2x2-es Cr-mintát is használ; ezt a TJpgDec
 * nem támogatja. Az upstream fejléc változatlan marad. Csak a JPEG-kód
 * fordul, minden foglalás PSRAM, a forrás legfeljebb 512x512 pixel.
 * A legnagyobb (4 komponenses progresszív) átmeneti munkatér is 4 MiB
 * alatt marad; nincs új feladat, állandó puffer vagy hardver-JPEG használat. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS LX_AUDIO_MEDIA_SOURCE_MAX
#define STBI_MALLOC(size) audio_jpeg_alloc(size)
#define STBI_FREE(pointer) audio_jpeg_free(pointer)
#define STBI_REALLOC_SIZED(pointer, old_size, new_size) audio_jpeg_realloc((pointer), (new_size))
/* A fejléc nyilvános, itt nem használt belépési pontjai nem részei az
 * alkalmazásnak. A figyelmeztetés-kivétel kizárólag az upstream fejlécé. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-value"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

typedef struct
{
    const uint8_t *bytes;
    size_t length, offset;
    lx_audio_image_t *image;
} audio_jpeg_t;

static size_t audio_jpeg_read(JDEC *decoder, uint8_t *destination, size_t length)
{
    audio_jpeg_t *input = decoder->device;
    if (length > input->length - input->offset)
        length = input->length - input->offset;
    if (destination)
        memcpy(destination, input->bytes + input->offset, length);
    input->offset += length;
    return length;
}

static int audio_jpeg_write(JDEC *decoder, void *bitmap, JRECT *rectangle)
{
    audio_jpeg_t *input = decoder->device;
    lx_audio_image_t *image = input->image;
    unsigned x0 = (rectangle->left * image->width + decoder->width - 1) / decoder->width;
    unsigned y0 = (rectangle->top * image->height + decoder->height - 1) / decoder->height;
    unsigned x1 = ((rectangle->right + 1) * image->width + decoder->width - 1) / decoder->width;
    unsigned y1 = ((rectangle->bottom + 1) * image->height + decoder->height - 1) / decoder->height;
    unsigned block_width = rectangle->right - rectangle->left + 1;
    for (unsigned y = y0; y < y1 && y < image->height; ++y)
        for (unsigned x = x0; x < x1 && x < image->width; ++x)
        {
            unsigned source_x = x * decoder->width / image->width - rectangle->left;
            unsigned source_y = y * decoder->height / image->height - rectangle->top;
            const uint8_t *pixel = (const uint8_t *)bitmap + (source_y * block_width + source_x) * 3;
            /* A projektben rögzített LVGL TJpgDec BGR888 sorrendet ad. */
            image->pixels[y * image->width + x] = audio_rgb565(pixel[2], pixel[1], pixel[0], 255);
        }
    return 1;
}

static lx_audio_image_t *audio_jpeg_decode(const uint8_t *data, size_t length, unsigned size,
                                           bool *unsupported)
{
    JDEC *decoder = audio_image_alloc(sizeof(*decoder));
    void *workspace = audio_image_alloc(16384);
    audio_jpeg_t input = {.bytes = data, .length = length};
    JRESULT prepared = decoder && workspace ?
        jd_prepare(decoder, audio_jpeg_read, workspace, 16384, &input) : JDR_MEM1;
    *unsupported = prepared == JDR_FMT2 || prepared == JDR_FMT3;
    if (prepared == JDR_OK)
    {
        input.image = audio_image_create(decoder->width, decoder->height, size,
                                          LX_AUDIO_MEDIA_JPEG_SOURCE_MAX);
        if (input.image && jd_decomp(decoder, audio_jpeg_write, 0) != JDR_OK)
        {
            lx_audio_image_free(input.image);
            input.image = NULL;
        }
    }
    free(workspace);
    free(decoder);
    return input.image;
}

static lx_audio_image_t *audio_jpeg_fallback(const uint8_t *data, size_t length, unsigned size)
{
    int width = 0, height = 0, channels = 0;
    uint8_t *rgb = stbi_load_from_memory(data, (int)length, &width, &height, &channels, 3);
    lx_audio_image_t *image = rgb ? audio_image_create((unsigned)width, (unsigned)height,
                                                       size, LX_AUDIO_MEDIA_SOURCE_MAX) : NULL;
    for (unsigned y = 0; image && y < image->height; ++y)
        for (unsigned x = 0; x < image->width; ++x)
        {
            unsigned source_x = x * (unsigned)width / image->width;
            unsigned source_y = y * (unsigned)height / image->height;
            const uint8_t *pixel = rgb + ((size_t)source_y * (unsigned)width + source_x) * 3;
            image->pixels[y * image->width + x] = audio_rgb565(pixel[0], pixel[1], pixel[2], 255);
        }
    stbi_image_free(rgb);
    return image;
}
#endif

lx_audio_image_t *lx_audio_image_decode(const uint8_t *data, size_t length, unsigned size)
{
    if (!data || length < 8 || length > LX_AUDIO_MEDIA_BYTES_MAX || !size || size > 256)
        return NULL;
    if (!memcmp(data, "\211PNG\r\n\032\n", 8))
        return audio_png_decode(data, length, size);
#if LV_USE_TJPGD
    if (data[0] == 0xff && data[1] == 0xd8)
    {
        bool unsupported = false;
        lx_audio_image_t *image = audio_jpeg_decode(data, length, size, &unsupported);
        /* A normál út megőrzi az EOI utáni bájtok korábbi elfogadását.
         * Csonka JPEG viszont nem válhat a tartalékút kitöltött képévé. */
        bool complete = data[length - 2] == 0xff && data[length - 1] == 0xd9;
        return !image && unsupported && complete ? audio_jpeg_fallback(data, length, size) : image;
    }
#endif
    return NULL;
}
