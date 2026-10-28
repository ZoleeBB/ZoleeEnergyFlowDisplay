/* A nyers segédfájl: PNG kibontás, méretezés, RGB565/RGB565A8 és fejlécellenőrzés. */
#include "app_template_raw.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)n);
    assert(data && fread(data, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *size = (size_t)n;
    return data;
}

int main(int argc, char **argv)
{
    assert(argc >= 3);
    char name[80];
    assert(app_template_raw_name("day.png", 1280, 800, name, sizeof(name)));
    assert(!strcmp(name, "day-1280x800.raw"));
    char base[66];
    assert(app_template_raw_is_name("day-1280x800.raw", base) && !strcmp(base, "day.png"));
    assert(app_template_raw_is_name("flow_solar-56x56.raw", base) && !strcmp(base, "flow_solar.png"));
    assert(!app_template_raw_is_name("day.png", base));
    assert(!app_template_raw_is_name("day-1280x800.png", base));
    assert(!app_template_raw_is_name("../evil-8x8.raw", base));

    for (int i = 1; i < argc; i++)
    {
        size_t size = 0;
        unsigned char *png = load(argv[i], &size);
        unsigned w = (unsigned)((png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19]);
        unsigned h = (unsigned)((png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23]);
        uint8_t *raw = NULL;
        size_t raw_size = 0;
        assert(app_template_raw_build(png, size, w, h, &raw, &raw_size));
        app_template_raw_t info;
        /* Más forrásméret: a segédfájl nem használható. */
        assert(!app_template_raw_parse(raw, raw_size, w, h, (uint32_t)size + 1, &info));
        assert(app_template_raw_parse(raw, raw_size, w, h, (uint32_t)size, &info));
        assert(info.width == w && info.height == h && info.stride == w * 2);
        /* Sérült fejléc és rossz méret elutasítva. */
        raw[0] ^= 1;
        assert(!app_template_raw_parse(raw, raw_size, w, h, (uint32_t)size, &info));
        raw[0] ^= 1;
        assert(!app_template_raw_parse(raw, raw_size, w + 1, h, (uint32_t)size, &info));
        assert(!app_template_raw_parse(raw, raw_size - 1, w, h, (uint32_t)size, &info));
        /* Egyetlen bitbillenés a képpontokban: a CRC kiszűri. */
        raw[APP_TEMPLATE_RAW_HEADER + raw_size / 2] ^= 0x10;
        assert(!app_template_raw_parse(raw, raw_size, w, h, (uint32_t)size, &info));
        raw[APP_TEMPLATE_RAW_HEADER + raw_size / 2] ^= 0x10;
        assert(app_template_raw_parse(raw, raw_size, w, h, (uint32_t)size, &info));
        char out[256];
        const char *slash = strrchr(argv[i], '/');
        const char *leaf = slash ? slash + 1 : argv[i];
        snprintf(out, sizeof(out), "%s.raw", leaf);
        FILE *f = fopen(out, "wb");
        assert(f && fwrite(raw, 1, raw_size, f) == raw_size);
        fclose(f);
        /* Fele méretre skálázva is érvényes marad. */
        uint8_t *small = NULL;
        size_t small_size = 0;
        assert(app_template_raw_build(png, size, w / 2, h / 2, &small, &small_size));
        assert(app_template_raw_parse(small, small_size, w / 2, h / 2, (uint32_t)size, &info));
        snprintf(out, sizeof(out), "%s.half.raw", leaf);
        f = fopen(out, "wb");
        assert(f && fwrite(small, 1, small_size, f) == small_size);
        fclose(f);
        free(small);
        free(raw);
        free(png);
    }
    puts("PASS: raw sidecar names, PNG decode, scaling, RGB565/RGB565A8 payload and header checks");
    return 0;
}
