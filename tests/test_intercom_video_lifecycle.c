/* Az igazi videómodul feladata nem foglalhat stacket csengetés nélkül. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/app_intercom.c"
static unsigned creates, deletes, sessions, decoders, audio_initializations;
static bool ringing, create_failure;
static bool credentials = true;
static bool audio_busy, fail_input, fail_decoder;
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
    (void)handle;
    assert(!strcmp(name, "intercom_video") && stack == 12288 && priority == 2 && core == 0);
    if (create_failure) {
        return 0;
    }
    scheduled = task;
    creates++;
    return pdPASS;
}
void vTaskDeleteWithCaps(void *task)
{
    assert(!task);
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
    *state = (app_intercom_snapshot_t){.ringing=ringing, .connected=true, .revision=1};
    strcpy(state->config.stream_url, "http://192.168.2.21/mjpg/video.mjpg");
    return true;
}
bool app_loxone_intercom_credentials(char username[96], char password[LX_PASSWORD_MAX])
{
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
    assert(pixels && width == 640 && height == 480 && stride == 1280 && revision == 1);
    displayed++;
    return true;
}
esp_err_t jpeg_decoder_get_info(const uint8_t *data, uint32_t size, jpeg_decode_picture_info_t *info)
{
    assert(data[0] == 0xff && size == 9);
    *info = (jpeg_decode_picture_info_t){.width=640, .height=480, .sample_method=JPEG_DOWN_SAMPLING_YUV444};
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
    memset(pixels, 0x55, 640 * 480 * 2);
    *written = 640 * 480 * 2;
    return ESP_OK;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration)
{
    assert(!strcmp(configuration->url, "http://192.168.2.21/mjpg/video.mjpg"));
    assert(configuration->disable_auto_redirect && configuration->auth_type == HTTP_AUTH_TYPE_NONE);
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
    assert(app_intercom_start() == ESP_OK);
    assert(audio_initializations == 1 && creates == 0 && sessions == 0);
    for (unsigned i = 0; i < 3; i++) {
        ringing = true;
        app_intercom_poll();
        app_intercom_poll();
        assert(creates == i + 1 && s_video_running);
        scheduled(NULL);
        assert(!s_video_running && deletes == creates && !decoders);
    }
    create_failure = true;
    ringing = true;
    app_intercom_poll();
    assert(!s_video_running && creates == 3 && deletes == 3);
    create_failure = false;
    now += 2000000;
    app_intercom_poll();
    scheduled(NULL);
    assert(!s_video_running && creates == 4 && deletes == 4 && sessions == 4 && !decoders);
    assert(displayed == 4);
    for (scenario = 1; scenario <= 4; scenario++) {
        ringing = true;
        unsigned before = displayed, previous_auths = auths;
        app_intercom_poll();
        scheduled(NULL);
        assert(!s_video_running && !decoders);
        if (scenario == 1) {
            assert(requests == 2 && auths == previous_auths + 1 && displayed == before + 1 && cleared);
        } else {
            assert(requests <= 2 && displayed == before && last_status[0]);
            if (scenario == 2) assert(requests == 2 && strstr(last_status, "401"));
        }
    }
    credentials = false;
    app_intercom_snapshot_t state = {.revision=1};
    video_session(&state);
    assert(strstr(last_status, "Hiányzó Loxone"));
    assert(app_intercom_idle());
    audio_busy = true;
    assert(!app_intercom_idle());
    audio_busy = false;
    credentials = true;
    fail_input = true;
    video_session(&state);
    assert(strstr(last_status, "256 KiB PSRAM"));
    fail_input = false;
    fail_decoder = true;
    video_session(&state);
    assert(strstr(last_status, "JPEG-dekóder") && strstr(last_status, "2048 B") && strstr(last_status, "1024 B"));
    puts("PASS: actual video startup creates no task; ringing starts one worker, decoder/session close and task deletes after bell end, repeated calls and task allocation failure recover; fragmented MJPEG reaches display, 401 challenge retries once, cached body clears, denied/redirect/header/credential errors");
    return 0;
}
