/* Valós baseline színes/szürke JPEG; a nagy APP-metaadat a régi 256 KiB-os
 * korlátot is átlépi. Az elkészült kép minden bájtjának meg kell maradnia. */
#include "app_intercom_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *jpeg;
    size_t size;
    unsigned frames;
} expected_t;

static void frame(void *context, const uint8_t *jpeg, size_t size)
{
    expected_t *expected = context;
    assert(size == expected->size && !memcmp(jpeg, expected->jpeg, size));
    expected->frames++;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const size_t chunks[] = {1, 61, 4096};
    for (int arg = 1; arg < argc; arg++) {
        FILE *file = fopen(argv[arg], "rb");
        assert(file && fseek(file, 0, SEEK_END) == 0);
        long length = ftell(file);
        assert(length > 256 * 1024 && (size_t)length < APP_INTERCOM_JPEG_MAX);
        rewind(file);
        expected_t expected = {.size = (size_t)length, .jpeg = malloc((size_t)length)};
        assert(expected.jpeg && fread(expected.jpeg, 1, expected.size, file) == expected.size);
        fclose(file);
        for (unsigned small = 0; small < 2; small++) {
            size_t capacity = small ? 256 * 1024 : APP_INTERCOM_JPEG_MAX;
            uint8_t *buffer = malloc(capacity);
            assert(buffer);
            for (unsigned chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); chunk++) {
                expected.frames = 0;
                app_intercom_stream_t stream = {.buffer = buffer, .capacity = capacity};
                for (unsigned repeat = 0; repeat < 2; repeat++) {
                    const char *mime = "\r\n--camera\r\nContent-Type: image/jpeg\r\n\r\n";
                    app_intercom_stream_feed(&stream, (const uint8_t *)mime, strlen(mime), frame, &expected);
                    for (size_t position = 0; position < expected.size; position += chunks[chunk]) {
                        size_t count = expected.size - position;
                        if (count > chunks[chunk]) count = chunks[chunk];
                        app_intercom_stream_feed(&stream, expected.jpeg + position, count, frame, &expected);
                    }
                }
                assert(expected.frames == (small ? 0U : 2U));
                assert(stream.frame_starts >= 2 && stream.oversized_frames == (small ? 2U : 0U));
            }
            free(buffer);
        }
        free(expected.jpeg);
    }
    puts("PASS: real baseline color/gray JPEG, valid >256 KiB APP metadata, split TCP, byte-exact frames and bounded overflow diagnostics");
}
