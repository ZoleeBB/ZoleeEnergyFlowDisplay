/* Az igazi videómodul feladata nem foglalhat stacket csengetés nélkül. */
#include <assert.h>
#include <setjmp.h>
static jmp_buf worker_exit;
#include <stdio.h>
#include <string.h>
#include "../main/app_intercom.c"
static unsigned creates, deletes, sessions, decoders, audio_initializations;
static bool ringing, create_failure, audio_view;
static bool credentials = true;
static bool audio_busy, fail_input, fail_decoder, gray_picture;
static unsigned scenario, requests, auths, cleared, displayed, offset;
static int64_t now = 1000000;
static char last_status[256];
/* Valós JPEG-markerfolyam, darabolt MIME-fejléccel; a JPEG hardvert helyettesítjük. */
static const uint8_t body[] = "--frame\r\nContent-Type: image/jpeg\r\n\r\n\xff\xd8\xff\xda\x00\x02\x01\xff\xd9\r\n";
static void (*scheduled)(void *);
size_t heap_caps_get_free_size(unsigned caps) { assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)); return 2048; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 1024; }
bool app_intercom_audio_busy(void) { return audio_busy; }
void mbedtls_platform_zeroize(void *buffer, size_t size)
{
    volatile unsigned char *bytes = buffer;
    while (size--) {
        *bytes++ = 0;
    }
}
int xTaskCreatePinnedToCoreWithCaps(void (*task)(void *), const char *name, unsigned stack,
                           void *argument, unsigned priority, void *handle, unsigned core, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    (void)argument;
    assert(handle);
    assert(!strcmp(name, "intercom_video") && stack == 12288 && priority == 2 && core == 0);
    if (create_failure) {
        return 0;
    }
    scheduled = task;
    *(TaskHandle_t *)handle = (void *)1;
    creates++;
    return pdPASS;
}
void vTaskSuspend(TaskHandle_t task)
{
    assert(!task);
    longjmp(worker_exit, 1);
}
void vTaskDeleteWithCaps(void *task)
{
    assert(task == (void *)1);
    deletes++;
}
void vTaskDelay(unsigned ticks) { (void)ticks; }
int64_t esp_timer_get_time(void) { return now; }
esp_err_t app_intercom_audio_start(void)
{
    audio_initializations++;
    return ESP_OK;
}
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *state)
{
    *state = (app_intercom_snapshot_t){.ringing=ringing, .audio_active=audio_view, .connected=true, .revision=1};
    strcpy(state->config.stream_url, "http://192.168.2.21/mjpg/video.mjpg");
    return true;
}
bool app_loxone_intercom_credentials(unsigned revision, char username[96], char password[LX_PASSWORD_MAX])
{
    assert(revision == 1);
    strcpy(username, "test");
    strcpy(password, "test");
    return credentials;
}
void app_intercom_ui_status(const char *message, unsigned revision)
{
    assert(message && revision == 1);
    snprintf(last_status, sizeof(last_status), "%s", message);
}
bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height, unsigned stride, unsigned revision)
{
    assert(pixels && revision == 1);
    if (gray_picture) {
        assert(width == 641 && height == 479 && stride == 656 * 2);
        for (unsigned y = 0; y < height; y++) {
            for (unsigned x = 0; x < width; x++) {
                unsigned gray = (x + y * 3) & 255U;
                uint16_t rgb = (uint16_t)(((gray & 0xf8U) << 8) | ((gray & 0xfcU) << 3) | (gray >> 3));
                assert(pixels[y * stride + x * 2] == (uint8_t)rgb);
                assert(pixels[y * stride + x * 2 + 1] == (uint8_t)(rgb >> 8));
            }
        }
    } else assert(width == 640 && height == 480 && stride == 1280);
    displayed++;
    return true;
}
esp_err_t jpeg_decoder_get_info(const uint8_t *data, uint32_t size, jpeg_decode_picture_info_t *info)
{
    assert(data[0] == 0xff && size == 9);
    *info = gray_picture ? (jpeg_decode_picture_info_t){.width=641, .height=479, .sample_method=JPEG_DOWN_SAMPLING_GRAY} :
        (jpeg_decode_picture_info_t){.width=640, .height=480, .sample_method=JPEG_DOWN_SAMPLING_YUV444};
    return ESP_OK;
}
void *jpeg_alloc_decoder_mem(size_t size, const jpeg_decode_memory_alloc_cfg_t *configuration, size_t *capacity)
{
    (void)configuration;
    *capacity = size;
    return fail_input ? NULL : malloc(size);
}
esp_err_t jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *configuration, jpeg_decoder_handle_t *handle)
{
    assert(configuration->timeout_ms == 100);
    if (fail_decoder) return ESP_ERR_NO_MEM;
    *handle = (void *)1;
    decoders++;
    return ESP_OK;
}
esp_err_t jpeg_del_decoder_engine(jpeg_decoder_handle_t handle)
{
    assert(handle == (void *)1 && decoders);
    decoders--;
    return ESP_OK;
}
esp_err_t jpeg_decoder_process(jpeg_decoder_handle_t handle, const jpeg_decode_cfg_t *configuration,
    const uint8_t *input, uint32_t size, uint8_t *pixels, uint32_t capacity, uint32_t *written)
{
    assert(handle && configuration && input && size == 9 && pixels && capacity >= 640 * 480 * 2);
    if (gray_picture) {
        assert(configuration->output_format == JPEG_DECODE_OUT_FORMAT_GRAY && capacity >= 648 * 480);
        for (unsigned y = 0; y < 480; y++)
            for (unsigned x = 0; x < 648; x++) pixels[y * 648 + x] = (uint8_t)(x + y * 3);
        *written = 648 * 480;
    } else {
        assert(configuration->output_format == JPEG_DECODE_OUT_FORMAT_RGB565);
        memset(pixels, 0x55, 640 * 480 * 2);
        *written = 640 * 480 * 2;
    }
    return ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration)
{
    assert(!strcmp(configuration->url, "http://192.168.2.21/mjpg/video.mjpg"));
    assert(configuration->disable_auto_redirect && configuration->auth_type == HTTP_AUTH_TYPE_BASIC);
    assert(!strcmp(configuration->username, "test") && !strcmp(configuration->password, "test"));
    sessions++;
    requests = offset = 0;
    return (void *)1;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t handle, int length)
{
    assert(handle && !length);
    requests++;
    return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t handle)
{
    (void)handle;
    if (scenario == 4) { ringing = false; return -1; }
    return 0;
}
esp_err_t esp_http_client_get_header(esp_http_client_handle_t handle, const char *name, char **value)
{
    assert(handle && !strcmp(name, "Authorization"));
    *value = "Basic dGVzdDp0ZXN0";
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t handle)
{
    (void)handle;
    if (scenario == 1 && requests == 1) return 401;
    if (scenario == 2) { if (requests == 2) ringing = false; return 401; }
    if (scenario == 3) { ringing = false; return 302; }
    return 200;
}
esp_err_t esp_http_client_add_auth(esp_http_client_handle_t handle)
{
    assert(handle && requests == 1);
    auths++;
    return ESP_OK;
}
esp_err_t esp_http_client_clear_response_buffer(esp_http_client_handle_t handle)
{
    assert(handle);
    cleared++;
    return ESP_OK;
}
int esp_http_client_read(esp_http_client_handle_t handle, char *buffer, int size)
{
    assert(handle && buffer && size > 0);
    if (offset < sizeof(body) - 1) {
        *buffer = body[offset++];
        return 1;
    }
    ringing = false;
    return 0;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t handle) { assert(handle); return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t handle) { assert(handle); return ESP_OK; }
int main(void)
{
    /* Korai foglalás: az összes későbbi hívás ugyanazt a motort használja.
     * Csengetéskor már szimuláltan nincs hely új motor létrehozására. */
    assert(app_intercom_prepare_video() == ESP_OK && decoders == 1);
    fail_decoder = true;
    assert(app_intercom_start() == ESP_OK);
    assert(audio_initializations == 1 && creates == 0 && sessions == 0);
    audio_view = true;
    assert(view_still_open(1) && !view_still_open(2));
    audio_view = false;
    assert(!view_still_open(1));
    for (unsigned i = 0; i < 3; i++) {
        ringing = true;
        app_intercom_poll();
        app_intercom_poll();
        assert(creates == i + 1 && s_video_running);
        if (!setjmp(worker_exit)) scheduled(NULL);
        assert(s_video_running && s_video_task.finished);
        assert(app_intercom_idle());
        assert(!s_video_running && deletes == creates && decoders == 1);
    }
    create_failure = true;
    ringing = true;
    app_intercom_poll();
    assert(!s_video_running && creates == 3 && deletes == 3);
    create_failure = false;
    now += 2000000;
    app_intercom_poll();
    if (!setjmp(worker_exit)) scheduled(NULL);
    assert(app_intercom_idle());
    assert(!s_video_running && creates == 4 && deletes == 4 && sessions == 4 && decoders == 1);
    assert(displayed == 4);
    for (scenario = 1; scenario <= 4; scenario++) {
        ringing = true;
        unsigned before = displayed, previous_auths = auths;
        app_intercom_poll();
        if (!setjmp(worker_exit)) scheduled(NULL);
        assert(s_video_running && s_video_task.finished);
        assert(app_intercom_idle());
        assert(!s_video_running && decoders == 1);
        if (scenario == 1) {
            assert(requests == 2 && auths == previous_auths + 1 && displayed == before + 1 && cleared);
        } else {
            assert(requests <= 2 && displayed == before && last_status[0]);
            if (scenario == 2) assert(requests == 2 && strstr(last_status, "401 (Basic)") && strstr(last_status, "videós admin"));
        }
    }
    credentials = false;
    app_intercom_snapshot_t state = {.revision=1};
    video_session(&state);
    assert(strstr(last_status, "Hiányzó videós"));
    assert(app_intercom_idle());
    audio_busy = true;
    assert(!app_intercom_idle());
    audio_busy = false;
    credentials = true;
    fail_input = true;
    video_session(&state);
    assert(strstr(last_status, "2 MiB PSRAM"));
    fail_input = false;
    assert(jpeg_del_decoder_engine(s_decoder) == ESP_OK);
    s_decoder = NULL;
    fail_decoder = true;
    video_session(&state);
    assert(strstr(last_status, "JPEG-dekóder") && strstr(last_status, "2048 B") && strstr(last_status, "1024 B"));
    fail_decoder = false;
    scenario = 0; gray_picture = true; ringing = true;
    unsigned previous_displayed = displayed;
    strcpy(state.config.stream_url, "http://192.168.2.21/mjpg/video.mjpg");
    video_session(&state);
    assert(displayed == previous_displayed + 1 && decoders == 1);
    assert(jpeg_del_decoder_engine(s_decoder) == ESP_OK);
    s_decoder = NULL;
    puts("PASS: padded grayscale JPEG to RGB565 in-place,  preemptive Basic with separate credentials, auth-method diagnostics, external deletion without cleanup task, actual video startup creates no task; ringing starts one worker, decoder/session close and task deletes after bell end, repeated calls and task allocation failure recover; fragmented MJPEG reaches display, 401 challenge retries once, cached body clears, denied/redirect/header/credential errors");
    return 0;
}
