#include "app_loxone_audio_media.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    uint8_t *bytes = malloc((size_t)length);
    assert(bytes && fread(bytes, 1, length, file) == (size_t)length);
    fclose(file);
    lx_audio_image_t *image = lx_audio_image_decode(bytes, length, (unsigned)atoi(argv[2]));
    free(bytes);
    if (!image)
    {
        puts("REJECTED");
        return 0;
    }
    printf("%u %u\n", image->width, image->height);
    for (unsigned i = 0; i < image->width * image->height; ++i)
        printf("%04x%s", image->pixels[i], i + 1 == image->width * image->height ? "\n" : " ");
    lx_audio_image_free(image);
    return 0;
}
