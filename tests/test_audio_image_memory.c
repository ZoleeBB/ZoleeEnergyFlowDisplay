/* A valódi dekóderek PSRAM-foglalásai: minden hibapont és a kemény keret.
 * A képek saját tesztminták, nem továbbterjesztett rádió-/albumborítók. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "src/libs/tjpgd/tjpgd.h"

typedef struct
{
    void *pointer;
    void *base;
    size_t bytes;
} allocation_t;
static allocation_t allocations[64];
static size_t live_bytes, peak_bytes;
static unsigned allocation_calls, failure_at;

static void *tracked_calloc(size_t alignment, size_t count, size_t size, unsigned caps,
                           bool weak_alignment)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(count && size <= SIZE_MAX / count);
    assert(alignment && !(alignment & (alignment - 1)));
    assert(alignment >= _Alignof(void *));
    if (++allocation_calls == failure_at)
        return NULL;
    /* A P4 alapheapje a max_align_t-nál gyengébben igazít. A hoston legalább
     * pointer-igazítás kell, de szándékosan eltoljuk a max_align_t határától.
     * A valódi malloc-alap külön marad, a modell fejléce nem függ az igazítástól. */
    size_t base_alignment = weak_alignment ? _Alignof(max_align_t) : alignment;
    size_t offset = weak_alignment ? _Alignof(void *) : 0;
    size_t padding = base_alignment - 1 + offset;
    size_t bytes = count * size;
    assert(bytes <= SIZE_MAX - padding);
    void *base = calloc(1, bytes + padding);
    assert(base);
    uintptr_t address = ((uintptr_t)base + base_alignment - 1) & ~(uintptr_t)(base_alignment - 1);
    address += offset;
    void *pointer = (void *)address;
    assert(address % alignment == 0);
    if (weak_alignment)
        assert(address % _Alignof(max_align_t) != 0);
    for (unsigned i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i)
        if (!allocations[i].pointer)
        {
            allocations[i] = (allocation_t){pointer, base, bytes};
            live_bytes += bytes;
            if (live_bytes > peak_bytes)
                peak_bytes = live_bytes;
            return pointer;
        }
    assert(!"Allocation tracker exhausted");
    return NULL;
}

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    return tracked_calloc(_Alignof(void *), count, size, caps, true);
}

void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, unsigned caps)
{
    return tracked_calloc(alignment, count, size, caps, false);
}

static void tracked_free(void *pointer)
{
    if (!pointer)
        return;
    for (unsigned i = 0; i < sizeof(allocations) / sizeof(allocations[0]); ++i)
        if (allocations[i].pointer == pointer)
        {
            live_bytes -= allocations[i].bytes;
            free(allocations[i].base);
            allocations[i] = (allocation_t){0};
            return;
        }
    assert(!"Untracked decoder allocation");
}

#define ESP_PLATFORM
#define free tracked_free
#if defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#include "../main/app_loxone_audio_image.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef free

static void jpeg_alignment_test(void)
{
    const size_t alignment = _Alignof(audio_jpeg_allocation_t);
    assert(_Alignof(void *) < alignment);
    void *plain = heap_caps_calloc(1, 33, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(plain && (uintptr_t)plain % _Alignof(void *) == 0);
    assert((uintptr_t)plain % alignment != 0);
    tracked_free(plain);

    const size_t sizes[] = {1, 2, 3, 7, 16, 31, 33, 63, 257};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        void *pointer = audio_jpeg_alloc(sizes[i]);
        assert(pointer && (uintptr_t)pointer % alignment == 0);
        assert((uintptr_t)((audio_jpeg_allocation_t *)pointer - 1) % alignment == 0);
        memset(pointer, 0x5a, sizes[i]);
        void *grown = audio_jpeg_realloc(pointer, sizes[i] + 5);
        assert(grown && (uintptr_t)grown % alignment == 0);
        assert((uintptr_t)((audio_jpeg_allocation_t *)grown - 1) % alignment == 0);
        for (size_t j = 0; j < sizes[i]; ++j)
            assert(((unsigned char *)grown)[j] == 0x5a);
        audio_jpeg_free(grown);
    }
    assert(!live_bytes && !atomic_load(&audio_jpeg_working_bytes));
    printf("PASS: JPEG headers and payloads aligned to %zu bytes with %zu-byte default heap alignment\n",
           alignment, _Alignof(void *));
}

int main(int argc, char **argv)
{
    assert(argc == 4);
    jpeg_alignment_test();
    for (int input = 1; input < argc; ++input)
    {
        FILE *file = fopen(argv[input], "rb");
        assert(file);
        fseek(file, 0, SEEK_END);
        size_t length = (size_t)ftell(file);
        rewind(file);
        uint8_t *bytes = malloc(length);
        assert(bytes && fread(bytes, 1, length, file) == length);
        fclose(file);

        failure_at = allocation_calls = 0;
        peak_bytes = 0;
        lx_audio_image_t *image = lx_audio_image_decode(bytes, length, 256);
        assert(image && image->width == 256 && image->height == 256);
        lx_audio_image_free(image);
        assert(!live_bytes && !atomic_load(&audio_jpeg_working_bytes));
        assert(peak_bytes <= AUDIO_JPEG_WORKING_MAX + 256 * 256 * 2 + sizeof(*image));
        unsigned count = allocation_calls;
        printf("PASS: JPEG source %d, %u PSRAM allocations, peak %zu bytes\n", input, count, peak_bytes);

        for (unsigned fail = 1; fail <= count; ++fail)
        {
            allocation_calls = 0;
            failure_at = fail;
            image = lx_audio_image_decode(bytes, length, 256);
            lx_audio_image_free(image);
            assert(!live_bytes && !atomic_load(&audio_jpeg_working_bytes));
        }
        free(bytes);
    }

    failure_at = 0;
    void *full = audio_jpeg_alloc(AUDIO_JPEG_WORKING_MAX - sizeof(audio_jpeg_allocation_t));
    assert(full && !audio_jpeg_alloc(1));
    audio_jpeg_free(full);
    void *small = audio_jpeg_alloc(32);
    assert(small);
    memset(small, 0x5a, 32);
    void *larger = audio_jpeg_realloc(small, 64);
    assert(larger && ((unsigned char *)larger)[31] == 0x5a);
    assert(!audio_jpeg_realloc(larger, AUDIO_JPEG_WORKING_MAX));
    assert(((unsigned char *)larger)[31] == 0x5a);
    audio_jpeg_free(larger);
    assert(!live_bytes && !atomic_load(&audio_jpeg_working_bytes));
    puts("PASS: all JPEG allocation failures release PSRAM; 4 MiB fallback cap and realloc accounting");
    return 0;
}

/* A bevont dekóder JPEG-only static prototípusainak hiányzó definícióját
 * GCC csak a teljes fordítási egység végén jelzi. A fenti saját tesztkód
 * az include utáni pop miatt változatlanul szigorú figyelmeztetésekkel él. */
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
