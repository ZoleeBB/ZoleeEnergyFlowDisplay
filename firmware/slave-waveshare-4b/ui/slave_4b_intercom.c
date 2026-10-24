/* 720×720-as Slave-nézet, közös Master MJPEG/SIP motorral. A kamera felbontása
 * nem változik; a két UI-pufferbe másolás közben kicsinyítünk. */
#include "slave_4b_intercom.h"
#include "app_intercom.h"
#include "app_intercom_audio.h"
#include "app_intercom_control.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(app_font_20);
LV_FONT_DECLARE(app_font_28);
#define VIDEO_WIDTH 720U
#define VIDEO_HEIGHT 460U
#define VIDEO_BYTES (VIDEO_WIDTH * VIDEO_HEIGHT * 2U)
static portMUX_TYPE frame_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t *front, *spare;
static bool accepting, writing, ready, visible;
static unsigned generation, frame_width, frame_height, rendered_generation;
static char message[192];
static unsigned message_revision;
static lv_obj_t *overlay, *image, *status, *audio_status, *audio, *gate;
static lv_image_dsc_t descriptor;
static int64_t last_gate;

static lv_obj_t *label(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, 20, y);
    lv_obj_set_width(object, 680);
    lv_obj_set_style_text_font(object, &app_font_20, 0);
    lv_obj_set_style_text_color(object, lv_color_white(), 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    return object;
}

static void talk(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    app_intercom_audio_status_t state;
    app_intercom_audio_snapshot(&state);
    bool active = state.state == APP_INTERCOM_AUDIO_ACTIVE || state.state == APP_INTERCOM_AUDIO_CONNECTING;
    if (app_intercom_audio_request(!active, generation) != ESP_OK)
        lv_label_set_text(audio_status, "A hang nem indítható. Ellenőrizd a Master kapcsolatát.");
}

static void open_gate(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    int64_t now = esp_timer_get_time();
    if (last_gate && now - last_gate < 1000000) return;
    last_gate = now;
    if (app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) == ESP_OK)
        lv_obj_add_state(gate, LV_STATE_DISABLED);
}

static lv_obj_t *button(int x, lv_event_cb_t callback)
{
    lv_obj_t *object = lv_button_create(overlay);
    lv_obj_set_pos(object, x, 610);
    lv_obj_set_size(object, 330, 90);
    lv_obj_set_style_radius(object, 16, 0);
    lv_obj_set_style_text_font(object, &app_font_28, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title = lv_label_create(object);
    lv_obj_set_width(title, 306);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_center(title);
    return object;
}

void slave_4b_intercom_prepare(void)
{
    app_intercom_snapshot_t snapshot = {0};
    bool active = app_loxone_intercom_snapshot(&snapshot) && (snapshot.ringing || snapshot.audio_active);
    portENTER_CRITICAL(&frame_lock);
    bool need = active && !front && !spare;
    portEXIT_CRITICAL(&frame_lock);
    if (need) {
        uint8_t *a = heap_caps_malloc(VIDEO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        uint8_t *b = heap_caps_malloc(VIDEO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!a || !b) { free(a); free(b); }
        else {
            portENTER_CRITICAL(&frame_lock);
            front = a; spare = b;
            portEXIT_CRITICAL(&frame_lock);
        }
    }
    portENTER_CRITICAL(&frame_lock);
    if (generation != snapshot.revision) { ready = false; generation = snapshot.revision; }
    accepting = active && spare;
    bool cleanup = !active && !visible && !writing;
    portEXIT_CRITICAL(&frame_lock);
    if (active) app_intercom_poll();
    if (cleanup && app_intercom_idle()) {
        portENTER_CRITICAL(&frame_lock);
        uint8_t *a = front, *b = spare;
        front = spare = NULL;
        ready = false;
        portEXIT_CRITICAL(&frame_lock);
        free(a); free(b);
    }
}

bool slave_4b_intercom_update(void)
{
    app_intercom_snapshot_t snapshot = {0};
    bool active = app_loxone_intercom_snapshot(&snapshot) && (snapshot.ringing || snapshot.audio_active);
    if (!active) {
        if (overlay) {
            lv_obj_delete(overlay);
            overlay = image = status = audio_status = audio = gate = NULL;
            lv_image_cache_drop(&descriptor);
        }
        portENTER_CRITICAL(&frame_lock);
        accepting = ready = visible = false;
        portEXIT_CRITICAL(&frame_lock);
        return false;
    }
    if (!overlay) {
        overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, 720, 720);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        image = lv_image_create(overlay);
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        status = label(overlay, "Kapcsolódás a kamerához...", 12);
        audio_status = label(overlay, "", 555);
        gate = button(20, open_gate);
        audio = button(370, talk);
        lv_obj_set_style_bg_color(gate, lv_color_hex(0x347c27), 0);
        last_gate = 0;
        portENTER_CRITICAL(&frame_lock);
        visible = true;
        portEXIT_CRITICAL(&frame_lock);
    }
    if (rendered_generation != snapshot.revision) {
        rendered_generation = snapshot.revision;
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(status, "Kapcsolódás a kamerához...");
    }
    portENTER_CRITICAL(&frame_lock);
    bool have_frame = ready && !writing && front && generation == snapshot.revision;
    portEXIT_CRITICAL(&frame_lock);
    if (have_frame) {
        lv_image_cache_drop(&descriptor);
        portENTER_CRITICAL(&frame_lock);
        have_frame = ready && !writing && generation == snapshot.revision;
        if (have_frame) {
            uint8_t *old = front; front = spare; spare = old;
            descriptor = (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_RGB565, .w = frame_width, .h = frame_height, .stride = frame_width * 2},
                .data_size = frame_width * frame_height * 2, .data = front};
            ready = false;
        }
        portEXIT_CRITICAL(&frame_lock);
        if (have_frame) {
            lv_image_set_src(image, &descriptor);
            lv_obj_set_pos(image, (720 - descriptor.header.w) / 2, 90 + (460 - descriptor.header.h) / 2);
            lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(status, "");
        }
    }
    char detail[192];
    portENTER_CRITICAL(&frame_lock);
    snprintf(detail, sizeof(detail), "%s", message_revision == snapshot.revision ? message : "");
    portEXIT_CRITICAL(&frame_lock);
    if (!snapshot.connected) lv_label_set_text(status, "A Master adatkapcsolata megszakadt.");
    else if (detail[0]) lv_label_set_text(status, detail);
    app_intercom_audio_status_t sound;
    app_intercom_audio_snapshot(&sound);
    bool talking = sound.state == APP_INTERCOM_AUDIO_ACTIVE || sound.state == APP_INTERCOM_AUDIO_CONNECTING;
    lv_label_set_text(lv_obj_get_child(audio, 0), talking ? "Beszélgetés vége" : "Beszélgetés");
    lv_obj_set_style_bg_color(audio, lv_color_hex(talking ? 0xa53030 : 0x246696), 0);
    bool blocked = snapshot.audio_elsewhere || sound.state == APP_INTERCOM_AUDIO_STOPPING || !snapshot.connected;
    if (blocked) lv_obj_add_state(audio, LV_STATE_DISABLED);
    else lv_obj_remove_state(audio, LV_STATE_DISABLED);
    lv_label_set_text(audio_status, snapshot.audio_elsewhere ? "Beszélgetés másik kijelzőn / várakozás a Masterre" :
        sound.revision == snapshot.revision && sound.state != APP_INTERCOM_AUDIO_RINGING ? sound.message : "");
    if (snapshot.gate_supported) {
        lv_obj_remove_flag(gate, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(gate, 0), snapshot.config.gate.name);
        if (!snapshot.connected || snapshot.command_pending) lv_obj_add_state(gate, LV_STATE_DISABLED);
        else lv_obj_remove_state(gate, LV_STATE_DISABLED);
    } else lv_obj_add_flag(gate, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(audio, snapshot.gate_supported ? 370 : 195);
    return true;
}

bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height, unsigned stride, unsigned revision)
{
    if (!pixels || !width || !height || width > 1280 || height > 720 || stride < width * 2) return false;
    portENTER_CRITICAL(&frame_lock);
    bool accepted = accepting && spare && !writing && generation == revision;
    uint8_t *destination = accepted ? spare : NULL;
    if (accepted) { writing = true; ready = false; }
    portEXIT_CRITICAL(&frame_lock);
    if (!accepted) return false;
    unsigned scale = width * VIDEO_HEIGHT > height * VIDEO_WIDTH ? VIDEO_WIDTH * 1024 / width : VIDEO_HEIGHT * 1024 / height;
    unsigned w = width * scale / 1024, h = height * scale / 1024;
    if (!w) w = 1;
    if (!h) h = 1;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = pixels + (y * height / h) * stride;
        for (unsigned x = 0; x < w; x++) {
            unsigned source = (x * width / w) * 2;
            destination[(y * w + x) * 2] = row[source];
            destination[(y * w + x) * 2 + 1] = row[source + 1];
        }
    }
    portENTER_CRITICAL(&frame_lock);
    accepted = accepting && generation == revision;
    if (accepted) { ready = true; frame_width = w; frame_height = h; message[0] = 0; }
    writing = false;
    portEXIT_CRITICAL(&frame_lock);
    return accepted;
}

void app_intercom_ui_status(const char *text, unsigned revision)
{
    portENTER_CRITICAL(&frame_lock);
    snprintf(message, sizeof(message), "%s", text ? text : "");
    message_revision = revision;
    portEXIT_CRITICAL(&frame_lock);
}
