/* Hardveres JPEG-háttér dekódolás; lásd app_jpeg_background.h. */
#include "app_jpeg_background.h"

#include <stdlib.h>
#include <string.h>

#if defined(CONFIG_IDF_TARGET_ESP32P4) && !defined(APP_TEMPLATE_DESKTOP)

#include "driver/jpeg_decode.h"
#include "esp_log.h"

static const char *TAG = "jpeg_bg";

void app_jpeg_background_free(app_jpeg_image_t *image)
{
    if (!image || !image->pixels)
        return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

bool app_jpeg_background_decode(const uint8_t *data, size_t size, unsigned expected_width,
                                unsigned expected_height, app_jpeg_image_t *out)
{
    if (!data || !out || size < 4 || !expected_width || !expected_height)
        return false;
    memset(out, 0, sizeof(*out));

    jpeg_decode_picture_info_t info = {0};
    if (jpeg_decoder_get_info(data, (uint32_t)size, &info) != ESP_OK)
        return false;
    /* A képpontra pontos elrendezés miatt csak a PNG-vel azonos méret jó. */
    if (info.width != expected_width || info.height != expected_height ||
        info.sample_method == JPEG_DOWN_SAMPLING_GRAY)
        return false;

    /*
     * Rövid életű dekóderpéldány: a kaputelefon saját motorját nem
     * zavarjuk, és témaváltások között nem tartunk belső RAM-ot foglalva.
     * Foglalt vagy hiányzó hardvernél a PNG-út marad.
     */
    jpeg_decoder_handle_t decoder = NULL;
    const jpeg_decode_engine_cfg_t engine = {.timeout_ms = 200};
    if (jpeg_new_decoder_engine(&engine, &decoder) != ESP_OK)
    {
        ESP_LOGW(TAG, "A JPEG-motor nem elérhető, marad a PNG háttér");
        return false;
    }

    unsigned stride = (info.width + 15U) & ~15U;
    size_t required = (size_t)stride * ((info.height + 15U) & ~15U) * 2U;
    size_t capacity = 0;
    jpeg_decode_memory_alloc_cfg_t memory = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
    uint8_t *pixels = jpeg_alloc_decoder_mem(required, &memory, &capacity);
    bool ok = false;
    if (pixels)
    {
        const jpeg_decode_cfg_t format = {
            .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
            /* Az LVGL little-endian RGB565-öt vár; a driver RGB beállítása
             * big-endian kimenetet jelent, ezért itt BGR a helyes. */
            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        };
        uint32_t written = 0;
        ok = jpeg_decoder_process(decoder, &format, data, (uint32_t)size, pixels,
                                  (uint32_t)capacity, &written) == ESP_OK &&
             written >= (size_t)stride * info.height * 2U;
        if (!ok)
            ESP_LOGW(TAG, "Sikertelen JPEG-dekódolás, marad a PNG háttér");
    }
    jpeg_del_decoder_engine(decoder);
    if (!ok)
    {
        free(pixels);
        return false;
    }
    *out = (app_jpeg_image_t){.pixels = pixels,
                              .size = capacity,
                              .width = info.width,
                              .height = info.height,
                              .stride = stride * 2U};
    return true;
}

#else /* Asztali build és nem P4 cél: nincs hardveres motor. */

void app_jpeg_background_free(app_jpeg_image_t *image)
{
    if (image)
        memset(image, 0, sizeof(*image));
}

bool app_jpeg_background_decode(const uint8_t *data, size_t size, unsigned expected_width,
                                unsigned expected_height, app_jpeg_image_t *out)
{
    (void)data;
    (void)size;
    (void)expected_width;
    (void)expected_height;
    if (out)
        memset(out, 0, sizeof(*out));
    return false;
}

#endif
