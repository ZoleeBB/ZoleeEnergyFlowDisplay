/* Az igazi videómodul feladata nem foglalhat stacket csengetés nélkül. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../main/app_intercom.c"
static unsigned creates, deletes, sessions, decoders, audio_initializations;
static bool ringing, create_failure;
static void (*scheduled)(void *);
void mbedtls_platform_zeroize(void *buffer, size_t size)
{
    volatile unsigned char *bytes = buffer;
    while (size--) {
        *bytes++ = 0;
    }
}
int xTaskCreatePinnedToCore(void (*task)(void *), const char *name, unsigned stack,
                           void *argument, unsigned priority, void *handle, unsigned core)
{
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
void vTaskDelete(void *task)
{
    assert(!task);
    deletes++;
}
void vTaskDelay(unsigned ticks) { (void)ticks; }
int64_t esp_timer_get_time(void) { return 1000000; }
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
    return true;
}
void app_intercom_ui_status(const char *message, unsigned revision)
{
    assert(message && revision == 1);
}
bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height, unsigned stride, unsigned revision)
{
    (void)pixels; (void)width; (void)height; (void)stride; (void)revision;
    return false;
}
esp_err_t jpeg_decoder_get_info(const uint8_t *data, uint32_t size, jpeg_decode_picture_info_t *info)
{
    (void)data; (void)size; (void)info;
    return ESP_FAIL;
}
void *jpeg_alloc_decoder_mem(size_t size, const jpeg_decode_memory_alloc_cfg_t *configuration, size_t *capacity)
{
    (void)configuration;
    *capacity = size;
    return malloc(size);
}
esp_err_t jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *configuration, jpeg_decoder_handle_t *handle)
{
    assert(configuration->timeout_ms == 100);
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
    (void)handle; (void)configuration; (void)input; (void)size; (void)pixels; (void)capacity; (void)written;
    return ESP_FAIL;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *configuration)
{
    assert(!strcmp(configuration->url, "http://192.168.2.21/mjpg/video.mjpg"));
    assert(configuration->disable_auto_redirect && configuration->auth_type == HTTP_AUTH_TYPE_BASIC);
    sessions++;
    return (void *)1;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t handle, int length)
{
    assert(handle && !length);
    return ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t handle) { (void)handle; return 0; }
int esp_http_client_get_status_code(esp_http_client_handle_t handle) { (void)handle; return 200; }
int esp_http_client_read(esp_http_client_handle_t handle, char *buffer, int size)
{
    assert(handle && buffer && size > 0);
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
    app_intercom_poll();
    scheduled(NULL);
    assert(!s_video_running && creates == 4 && deletes == 4 && sessions == 4 && !decoders);
    puts("PASS: actual video startup creates no task; ringing starts one worker, decoder/session close and task deletes after bell end, repeated calls and task allocation failure recover");
    return 0;
}
