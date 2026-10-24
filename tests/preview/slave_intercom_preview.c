/* A valódi Slave LVGL-nézet, képpuffer és eseménykezelők hardver nélküli
 * életciklustesztje. A médiamotor itt csak képkockát és állapotot ad. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../firmware/slave-waveshare-4b/ui/slave_4b_intercom.c"
static app_intercom_snapshot_t fixture;
static app_intercom_audio_status_t sound;
static unsigned starts, commands, audio_requests;
static bool idle = true;
int64_t esp_timer_get_time(void) { return 2000000; }
void *heap_caps_malloc(size_t size, unsigned caps) { (void)caps; return malloc(size); }
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out) { *out = fixture; return fixture.config.enabled; }
void app_intercom_audio_snapshot(app_intercom_audio_status_t *out) { *out = sound; }
bool app_intercom_idle(void) { return idle; }
void app_intercom_poll(void) { starts++; }
esp_err_t app_intercom_audio_request(bool active, unsigned revision)
{
    assert(active && revision == fixture.revision); audio_requests++; return ESP_OK;
}
esp_err_t app_loxone_home_command(const char *id, lx_home_command_t action)
{
    assert(!strcmp(id, LX_INTERCOM_GATE_ID) && action == LX_HOME_COMMAND_PULSE); commands++; return ESP_OK;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area; (void)pixels; lv_display_flush_ready(display);
}
int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(720, 720);
    static uint32_t draw[720 * 64];
    lv_display_set_buffers(display, draw, NULL, sizeof(draw), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    fixture.config.enabled = true; fixture.connected = true; fixture.ringing = true;
    fixture.gate_supported = true;
    strcpy(fixture.config.gate.name, "Kapunyitás");
    uint8_t *pixels = malloc(1280 * 720 * 2);
    for (size_t i = 0; i < 1280 * 720 * 2; i++) pixels[i] = (uint8_t)i;
    for (unsigned cycle = 1; cycle <= 100; cycle++) {
        fixture.revision = cycle;
        fixture.ringing = true;
        slave_4b_intercom_prepare();
        assert(front && spare && front != spare && accepting);
        assert(slave_4b_intercom_update());
        assert(!app_intercom_ui_frame(pixels, 1281, 720, 2562, cycle));
        assert(!app_intercom_ui_frame(pixels, 1280, 720, 2560, cycle - 1));
        assert(app_intercom_ui_frame(pixels, 1280, 720, 2560, cycle));
        assert(slave_4b_intercom_update());
        assert(descriptor.header.w == 720 && descriptor.header.h == 405);
        assert(descriptor.data_size == 720 * 405 * 2);
        assert(!lv_obj_has_flag(image, LV_OBJ_FLAG_HIDDEN));
        lv_obj_update_layout(overlay);
        assert(lv_obj_get_x(audio) >= 0 && lv_obj_get_x(audio) + lv_obj_get_width(audio) <= 720);
        fixture.audio_elsewhere = true; slave_4b_intercom_update();
        assert(lv_obj_has_state(audio, LV_STATE_DISABLED));
        fixture.audio_elsewhere = false; slave_4b_intercom_update();
        assert(!lv_obj_has_state(audio, LV_STATE_DISABLED));
        lv_obj_send_event(audio, LV_EVENT_CLICKED, NULL);
        lv_obj_send_event(gate, LV_EVENT_CLICKED, NULL);
        lv_obj_send_event(gate, LV_EVENT_CLICKED, NULL);
        assert(audio_requests == cycle && commands == cycle);
        lv_refr_now(display);
        fixture.ringing = false;
        assert(!slave_4b_intercom_update());
        slave_4b_intercom_prepare();
        assert(!front && !spare && !overlay && !accepting);
    }
    free(pixels);
    lv_display_delete(display);
    lv_deinit();
    printf("PASS: actual 720x720 Slave LVGL view, 100 video open/scale/render/cleanup cycles, bounded buffers, stale frame rejection, ownership buttons and gate debounce (%u starts)\n", starts);
    return 0;
}
