/* A kamera külön HTTP-kapcsolat, saját mentett videós fiókkal.
 * Sem hálózati várakozás, sem JPEG-dekódolás nem történik LVGL-lock alatt. */
#include "app_intercom.h"
#include "app_intercom_audio.h"
#include "app_intercom_stream.h"
#include "app_intercom_task.h"
#include "app_loxone.h"
#include "driver/jpeg_decode.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "app_intercom_build_config.h"
#include "mbedtls/platform_util.h"
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE s_video_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_ready, s_video_running;
static app_intercom_task_t s_video_task = APP_INTERCOM_TASK_INITIALIZER;

static void reap_video(void)
{
    if (app_intercom_task_reap(&s_video_task)) {
        portENTER_CRITICAL(&s_video_lock);
        s_video_running = false;
        portEXIT_CRITICAL(&s_video_lock);
    }
}
static int64_t s_next_attempt;
static const char *TAG = "intercom_video";

#define VIDEO_WIDTH 1280U
#define VIDEO_HEIGHT 720U

typedef struct
{
    jpeg_decoder_handle_t decoder;
    uint8_t *pixels;
    size_t pixels_capacity;
    unsigned revision;
    int64_t last_frame;
    unsigned jpeg_frames;
} video_decode_t;

static void decode_frame(void *context, const uint8_t *jpeg, size_t size)
{
    video_decode_t *video = context;
    video->jpeg_frames++;
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
    if (written < stride * info.height * 2U) {
        app_intercom_ui_status("Hiányos dekódolt kamerakép.", video->revision);
        return;
    }
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
    if (!app_loxone_intercom_credentials(state->revision, username, password)) {
        app_intercom_ui_status("Hiányzó videós felhasználónév vagy jelszó.", state->revision);
        goto credentials_done;
    }
    video_decode_t video = {.revision = state->revision};
    const jpeg_decode_engine_cfg_t engine = {.timeout_ms = 100};
    size_t input_capacity = 0;
    jpeg_decode_memory_alloc_cfg_t input_memory = {.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER};
    uint8_t *input = jpeg_alloc_decoder_mem(APP_INTERCOM_JPEG_MAX, &input_memory, &input_capacity);
    esp_http_client_handle_t http = NULL;
    if (!input) {
        app_intercom_ui_status("Nincs 256 KiB PSRAM a tömörített kameraképhez.", state->revision);
        goto done;
    }
    esp_err_t decoder_error = jpeg_new_decoder_engine(&engine, &video.decoder);
    if (decoder_error != ESP_OK) {
        char message[160];
        snprintf(message, sizeof(message), "JPEG-dekóder indítás (%d). Belső DMA: %u B szabad, %u B legnagyobb blokk.",
                 decoder_error, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        app_intercom_ui_status(message, state->revision);
        goto done;
    }
    app_intercom_stream_t stream = {.buffer = input, .capacity = APP_INTERCOM_JPEG_MAX};
    const esp_http_client_config_t options = {
        .url = state->config.stream_url,
        .username = username,
        .password = password,
        /* A Gen2 közvetlen videója Basic Authorization fejlécet várhat már
         * az első kérésben. 401 esetén a Digest kihívás továbbra is kezelhető. */
        .auth_type = HTTP_AUTH_TYPE_BASIC,
        .timeout_ms = 1500,
        .buffer_size = 4096,
        /* Más címre nem továbbítjuk a hitelesítési adatot. */
        .disable_auto_redirect = true,
    };
    http = esp_http_client_init(&options);
    if (!http) {
        app_intercom_ui_status("Nincs elég memória a kamera HTTP-kapcsolatához.", state->revision);
        goto done;
    }
    /* Az open/fetch/read streaming API nem végzi el a perform automatikus
     * 401 ismétlését. Egyszer követjük a kamera Basic/Digest kihívását,
     * azonos címen; átirányításkor sosem küldünk tovább jelszót. */
    int code = 0;
    const char *authentication = "hiányzó Authorization";
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        app_intercom_ui_status("Kamera HTTP-kapcsolat indítása...", state->revision);
        esp_err_t error = esp_http_client_open(http, 0);
        if (error != ESP_OK) {
            char message[128];
            snprintf(message, sizeof(message), "Kamera HTTP-kapcsolati hiba (%d). Újrapróbálás...", error);
            app_intercom_ui_status(message, state->revision);
            goto done;
        }
        char *authorization = NULL;
        if (esp_http_client_get_header(http, "Authorization", &authorization) == ESP_OK && authorization) {
            authentication = !strncmp(authorization, "Basic ", 6) ? "Basic" :
                             !strncmp(authorization, "Digest ", 7) ? "Digest" : "ismeretlen Authorization";
        }
        if (esp_http_client_fetch_headers(http) < 0) {
            app_intercom_ui_status("A kamera nem küldött HTTP-fejlécet. Újrapróbálás...", state->revision);
            goto done;
        }
        code = esp_http_client_get_status_code(http);
        ESP_LOGI(TAG, "Camera HTTP status=%d attempt=%u authentication=%s", code, attempt + 1, authentication);
        if (code != 401 || attempt != 0) {
            break;
        }
        if (esp_http_client_add_auth(http) != ESP_OK) {
            app_intercom_ui_status("A kamera hitelesítési módja nem támogatott.", state->revision);
            goto done;
        }
        esp_http_client_close(http);
        esp_http_client_clear_response_buffer(http);
        if (!still_ringing(state->revision)) {
            goto done;
        }
    }
    if (code != 200) {
        char message[128];
        snprintf(message, sizeof(message), "Kamera HTTP %d (%s). %s", code, authentication,
                 code == 401 || code == 403 ? "Ellenőrizd a külön videós admin adatokat." : "Nem érkezett videófolyam.");
        app_intercom_ui_status(message, state->revision);
        goto done;
    }
    app_intercom_ui_status("Kamera elérhető, az első MJPEG-képkockára várunk...", state->revision);
    uint8_t chunk[4096];
    int64_t started = esp_timer_get_time();
    size_t received = 0;
    while (still_ringing(state->revision))
    {
        int count = esp_http_client_read(http, (char *)chunk, sizeof(chunk));
        if (count == -ESP_ERR_HTTP_EAGAIN && esp_timer_get_time() - started < 10000000LL) {
            continue;
        }
        if (count <= 0)
            break;
        received += (size_t)count;
        app_intercom_stream_feed(&stream, chunk, (size_t)count, decode_frame, &video);
        if (esp_timer_get_time() - (video.last_frame ? video.last_frame : started) > 10000000LL)
            break;
        /* A stream nem veheti el a CPU0-t a közös Loxone-kapcsolattól. */
        vTaskDelay(1);
    }
    ESP_LOGI(TAG, "Camera bytes=%u JPEG=%u displayed=%d", (unsigned)received,
             video.jpeg_frames, video.last_frame != 0);
    if (still_ringing(state->revision) && (video.last_frame || !video.jpeg_frames)) {
        app_intercom_ui_status(video.last_frame ? "A videó megszakadt. Újracsatlakozás..." :
            received ? "Nem érkezett teljes MJPEG-kép (max. 256 KiB/kép). Újrapróbálás..." :
                       "A kamera nem küld videóadatot. Újrapróbálás...", state->revision);
    }
done:
    if (http)
    {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    if (video.decoder) jpeg_del_decoder_engine(video.decoder);
    free(video.pixels);
    free(input);
credentials_done:
    mbedtls_platform_zeroize(password, sizeof(password));
    mbedtls_platform_zeroize(username, sizeof(username));
}

static void worker(void *arg)
{
    (void)arg;
    for (;;)
    {
        app_intercom_snapshot_t state = {0};
        if (!app_loxone_intercom_snapshot(&state) || !state.ringing)
            break;
        video_session(&state);
        /* Hiba esetén is megszakítható, legfeljebb két másodperces újrapróbálás. */
        for (unsigned i = 0; i < 10; ++i)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            if (!still_ringing(state.revision)) break;
        }
    }
    app_intercom_task_finish(&s_video_task);
}

/* A 12 KiB stack PSRAM-ban él, és két csengetés között felszabadul. */
void app_intercom_poll(void)
{
    reap_video();
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_video_lock);
    bool available = s_ready && !s_video_running && now >= s_next_attempt;
    if (available) {
        s_video_running = true;
    }
    portEXIT_CRITICAL(&s_video_lock);
    if (!available) {
        return;
    }
    TaskHandle_t handle = NULL;
    if (xTaskCreatePinnedToCoreWithCaps(worker, "intercom_video", 12288, NULL, 2, &handle, 0,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        portENTER_CRITICAL(&s_video_lock);
        s_video_running = false;
        s_next_attempt = now + 2000000;
        portEXIT_CRITICAL(&s_video_lock);
        app_intercom_snapshot_t state;
        if (app_loxone_intercom_snapshot(&state)) {
            app_intercom_ui_status("Nincs elég memória a videófeladathoz. Újrapróbálás...", state.revision);
        }
    } else {
        app_intercom_task_created(&s_video_task, handle);
    }
}

bool app_intercom_idle(void)
{
    reap_video();
    portENTER_CRITICAL(&s_video_lock);
    bool running = s_video_running;
    portEXIT_CRITICAL(&s_video_lock);
    return !running && !app_intercom_audio_busy();
}

esp_err_t app_intercom_start(void)
{
    /* Induláskor nincs kamera-/hangfeladat; az engedélyezett hang DMA-ja előre foglalódik. A UI csak csengetéskor
     * indít videófeladatot, a hangfeladatot pedig kizárólag a beszélgetés gomb. */
    s_ready = true;
    return app_intercom_audio_start();
}
