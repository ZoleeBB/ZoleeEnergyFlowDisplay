/* A kamera külön HTTP-kapcsolat, de ugyanazt a mentett Loxone-fiókot használja.
 * Sem hálózati várakozás, sem JPEG-dekódolás nem történik LVGL-lock alatt. */
#include "app_intercom.h"
#include "app_intercom_audio.h"
#include "app_intercom_stream.h"
#include "app_loxone.h"
#include "driver/jpeg_decode.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/platform_util.h"
#include <stdlib.h>
#include <string.h>

#define VIDEO_WIDTH 1280U
#define VIDEO_HEIGHT 720U

typedef struct
{
    jpeg_decoder_handle_t decoder;
    uint8_t *pixels;
    size_t pixels_capacity;
    unsigned revision;
    int64_t last_frame;
} video_decode_t;

static void decode_frame(void *context, const uint8_t *jpeg, size_t size)
{
    video_decode_t *video = context;
    int64_t now = esp_timer_get_time();
    /* A hálózatot folyamatosan ürítjük, de legfeljebb 10 fps-t rajzolunk.
     * Így nincs késleltetést okozó, felhalmozódó képkockasor. */
    if (now - video->last_frame < 100000)
        return;
    jpeg_decode_picture_info_t info = {0};
    if (jpeg_decoder_get_info(jpeg, (uint32_t)size, &info) != ESP_OK ||
        !info.width || !info.height || info.width > VIDEO_WIDTH || info.height > VIDEO_HEIGHT ||
        info.sample_method == JPEG_DOWN_SAMPLING_GRAY)
    {
        app_intercom_ui_status("Nem támogatott kamerakép. Legfeljebb 1280×720 JPEG szükséges.", video->revision);
        return;
    }
    /* A P4 dekóder a sorokat és a magasságot 16 képpontra egészítheti ki.
     * A látható méretet megtartjuk, a sorlépést a kitöltéshez igazítjuk. */
    unsigned stride = (info.width + 15U) & ~15U;
    size_t required = stride * ((info.height + 15U) & ~15U) * 2U;
    if (required > video->pixels_capacity)
    {
        free(video->pixels);
        jpeg_decode_memory_alloc_cfg_t memory = {.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
        video->pixels = jpeg_alloc_decoder_mem(required, &memory, &video->pixels_capacity);
        if (!video->pixels)
        {
            video->pixels_capacity = 0;
            app_intercom_ui_status("Nincs elég memória a kameraképhez.", video->revision);
            return;
        }
    }
    const jpeg_decode_cfg_t format = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        /* Az LVGL RGB565 little-endian bájtsorrendet vár. A driver RGB
         * opciója big-endian kimenetet jelent, ezért itt BGR szükséges. */
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
    };
    uint32_t written = 0;
    if (jpeg_decoder_process(video->decoder, &format, jpeg, (uint32_t)size,
            video->pixels, (uint32_t)video->pixels_capacity, &written) != ESP_OK)
    {
        app_intercom_ui_status("A kamerakép dekódolása sikertelen. Új képre várunk...", video->revision);
        return;
    }
    /* YUV444 esetén a JPEG MCU nyolc, YUV422/420 esetén tizenhat pixel széles. */
    if (info.sample_method == JPEG_DOWN_SAMPLING_YUV444)
        stride = (info.width + 7U) & ~7U;
    if (written < stride * info.height * 2U)
        return;
    if (app_intercom_ui_frame(video->pixels, info.width, info.height, stride * 2U, video->revision))
        video->last_frame = now;
}

static bool still_ringing(unsigned revision)
{
    app_intercom_snapshot_t current;
    return app_loxone_intercom_snapshot(&current) && current.ringing && current.revision == revision;
}

static void video_session(const app_intercom_snapshot_t *state)
{
    char username[96] = {0}, password[LX_PASSWORD_MAX] = {0};
    if (!app_loxone_intercom_credentials(username, password))
        return;
    video_decode_t video = {.revision = state->revision};
    const jpeg_decode_engine_cfg_t engine = {.timeout_ms = 100};
    size_t input_capacity = 0;
    jpeg_decode_memory_alloc_cfg_t input_memory = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    uint8_t *input = jpeg_alloc_decoder_mem(APP_INTERCOM_JPEG_MAX, &input_memory, &input_capacity);
    esp_http_client_handle_t http = NULL;
    if (!input || jpeg_new_decoder_engine(&engine, &video.decoder) != ESP_OK)
    {
        app_intercom_ui_status("A videó nem indítható: nincs elég szabad memória.", state->revision);
        goto done;
    }
    app_intercom_stream_t stream = {.buffer = input, .capacity = APP_INTERCOM_JPEG_MAX};
    const esp_http_client_config_t options = {
        .url = state->config.stream_url,
        .username = username,
        .password = password,
        .auth_type = HTTP_AUTH_TYPE_BASIC,
        .timeout_ms = 1500,
        .buffer_size = 4096,
        /* Más címre nem továbbítjuk a hitelesítési adatot. */
        .disable_auto_redirect = true,
    };
    http = esp_http_client_init(&options);
    if (!http || esp_http_client_open(http, 0) != ESP_OK)
    {
        app_intercom_ui_status("A kamera nem érhető el. Újracsatlakozás...", state->revision);
        goto done;
    }
    (void)esp_http_client_fetch_headers(http);
    int code = esp_http_client_get_status_code(http);
    if (code != 200)
    {
        app_intercom_ui_status(code == 401 || code == 403 ?
            "A kamera elutasította a Loxone-fiókot. Ellenőrizd a jogosultságokat." :
            "A kamera nem adott videófolyamot. Újracsatlakozás...", state->revision);
        goto done;
    }
    uint8_t chunk[4096];
    int64_t started = esp_timer_get_time();
    while (still_ringing(state->revision))
    {
        int count = esp_http_client_read(http, (char *)chunk, sizeof(chunk));
        if (count <= 0)
            break;
        app_intercom_stream_feed(&stream, chunk, (size_t)count, decode_frame, &video);
        if (esp_timer_get_time() - (video.last_frame ? video.last_frame : started) > 10000000LL)
            break;
        /* A stream nem veheti el a CPU0-t a közös Loxone-kapcsolattól. */
        vTaskDelay(1);
    }
    if (still_ringing(state->revision))
        app_intercom_ui_status("A videó megszakadt. Újracsatlakozás...", state->revision);
done:
    if (http)
    {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    if (video.decoder) jpeg_del_decoder_engine(video.decoder);
    free(video.pixels);
    free(input);
    mbedtls_platform_zeroize(password, sizeof(password));
    mbedtls_platform_zeroize(username, sizeof(username));
}

static void worker(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_intercom_snapshot_t state = {0};
        if (app_loxone_intercom_snapshot(&state) && state.ringing)
            video_session(&state);
        /* Hiba esetén is megszakítható, legfeljebb két másodperces újrapróbálás. */
        for (unsigned i = 0; i < 10; ++i)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            if (!still_ringing(state.revision)) break;
        }
    }
}

esp_err_t app_intercom_start(void)
{
    if (xTaskCreatePinnedToCore(worker, "intercom_video", 12288, NULL, 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return app_intercom_audio_start();
}
