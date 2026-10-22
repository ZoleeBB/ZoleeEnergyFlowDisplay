/* Az app_ui.c része. A kaputelefon a felső rétegen él: a háttérben történő
 * téma-/hibaoldalváltás és a gesztus nem zárhatja be a csengetés közben. */
#if defined(ESP_PLATFORM) || defined(APP_TEMPLATE_DESKTOP)
#include "app_intercom.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define INTERCOM_FRONT_SIZE (1280U * 720U * 2U)
static lv_obj_t *intercom_overlay, *intercom_image, *intercom_message, *intercom_gate;
static uint8_t *intercom_front;
static uint8_t *intercom_prepared;
static lv_image_dsc_t intercom_descriptor;
static app_intercom_snapshot_t intercom_next;
static unsigned intercom_revision;
static bool intercom_has_gate;
static char intercom_video_status[LX_HOME_ERROR_SIZE];
static int64_t intercom_last_press;

static void intercom_open_gate(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    int64_t now = esp_timer_get_time();
    /* Egy érintés egy impulzus; nincs hosszú nyomásos ismétlés vagy offline sor. */
    if (intercom_last_press && now - intercom_last_press < 1000000LL)
        return;
    intercom_last_press = now;
    if (app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) != ESP_OK)
        lv_label_set_text(intercom_message, tr("A kapunyitás nem küldhető. Ellenőrizd a Loxone-kapcsolatot.",
                                             "Cannot send gate command. Check Loxone connection."));
    else
        lv_obj_add_state(intercom_gate, LV_STATE_DISABLED);
}

/* A nagy képpuffer foglalása LVGL-lock nélkül történik, csak aktív híváshoz.
 * A dekóder külön pufferbe dolgozik; az LVGL által olvasott kép nem változhat. */
static bool intercom_prepare(void)
{
    memset(&intercom_next, 0, sizeof(intercom_next));
    if (s_cfg.data_recept_mode != APP_DATA_LOXONE ||
        !app_loxone_intercom_snapshot(&intercom_next))
        return false;
    if (intercom_next.ringing && !intercom_front && !intercom_prepared)
    {
#ifdef ESP_PLATFORM
        intercom_prepared = heap_caps_malloc(INTERCOM_FRONT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        intercom_prepared = malloc(INTERCOM_FRONT_SIZE);
#endif
    }
    return intercom_next.ringing;
}

static void intercom_render(void)
{
    if (!intercom_next.ringing)
    {
        if (intercom_overlay)
        {
            lv_obj_delete(intercom_overlay);
            intercom_overlay = intercom_image = intercom_message = intercom_gate = NULL;
        }
        lv_image_cache_drop(&intercom_descriptor);
        free(intercom_front);
        free(intercom_prepared);
        intercom_front = intercom_prepared = NULL;
        memset(&intercom_descriptor, 0, sizeof(intercom_descriptor));
        return;
    }
    if (!intercom_overlay)
    {
        intercom_revision = intercom_next.revision;
        intercom_last_press = 0;
        intercom_front = intercom_prepared;
        intercom_prepared = NULL;
        strcpy(intercom_video_status, tr("Kapcsolódás a kamerához...", "Connecting to camera..."));
        intercom_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(intercom_overlay);
        lv_obj_set_size(intercom_overlay, W, H);
        lv_obj_set_style_bg_color(intercom_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(intercom_overlay, LV_OPA_COVER, 0);
        lv_obj_remove_flag(intercom_overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(intercom_overlay, LV_OBJ_FLAG_CLICKABLE);
        intercom_image = lv_image_create(intercom_overlay);
        lv_obj_remove_flag(intercom_image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_image_set_antialias(intercom_image, false);
        intercom_message = lbl(intercom_overlay, "", 24, 14, APP_FONT_20, 0xffffff);
        lv_obj_set_width(intercom_message, W - 48);
        lv_label_set_long_mode(intercom_message, LV_LABEL_LONG_WRAP);
        intercom_gate = lv_button_create(intercom_overlay);
        lv_obj_set_size(intercom_gate, 480, 92);
        lv_obj_align(intercom_gate, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_bg_color(intercom_gate, c(0x347c27), 0);
        lv_obj_set_style_radius(intercom_gate, 20, 0);
        lv_obj_set_style_text_font(intercom_gate, APP_FONT_28, 0);
        lv_obj_set_style_text_color(intercom_gate, lv_color_white(), 0);
        lv_obj_remove_flag(intercom_gate, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(intercom_gate, intercom_open_gate, LV_EVENT_CLICKED, NULL);
        lv_obj_t *title = lv_label_create(intercom_gate);
        /* A hosszabb, egyedi felirat sem lóghat túl az érinthető gombon. */
        lv_obj_set_size(title, 432, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(title, 72, 0);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(title);
    }
    if (!intercom_front && intercom_prepared)
    {
        intercom_front = intercom_prepared;
        intercom_prepared = NULL;
    }
    if (intercom_revision != intercom_next.revision)
    {
        intercom_revision = intercom_next.revision;
        lv_obj_add_flag(intercom_image, LV_OBJ_FLAG_HIDDEN);
        strcpy(intercom_video_status, tr("Kapcsolódás a kamerához...", "Connecting to camera..."));
    }
    bool gate = intercom_next.config.gate.control_uuid[0] != 0;
    intercom_has_gate = gate;
    if (gate)
    {
        lv_obj_remove_flag(intercom_gate, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(lv_obj_get_child(intercom_gate, 0), intercom_next.config.gate.name);
        if (!intercom_next.connected || !intercom_next.gate_supported || intercom_next.command_pending)
            lv_obj_add_state(intercom_gate, LV_STATE_DISABLED);
        else
            lv_obj_remove_state(intercom_gate, LV_STATE_DISABLED);
    }
    else
        lv_obj_add_flag(intercom_gate, LV_OBJ_FLAG_HIDDEN);
    const char *message = !intercom_front ? tr("Nincs elég memória a kameraképhez.", "Not enough memory for video.") :
        !intercom_next.connected ? tr("A Loxone-kapcsolat megszakadt. A csengetés végére várunk...",
                                    "Loxone disconnected. Waiting for ringing to end...") :
        intercom_last_press && esp_timer_get_time() - intercom_last_press < 3000000LL &&
            intercom_next.status[0] ? intercom_next.status : intercom_video_status;
    lv_label_set_text(intercom_message, message);
    /* A kapunyitó mindig a videó fölött marad; nincs bezáró gomb vagy időzítő. */
    lv_obj_move_foreground(intercom_message);
    lv_obj_move_foreground(intercom_gate);
}

bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height,
                           unsigned stride, unsigned revision)
{
    if (!pixels || !width || !height || width > 1280 || height > 720 || stride < width * 2U)
        return false;
    if (bsp_display_lock(100) != ESP_OK)
        return false;
    bool accepted = intercom_overlay && intercom_front && revision == intercom_revision;
    if (accepted)
    {
        lv_image_cache_drop(&intercom_descriptor);
        for (unsigned y = 0; y < height; ++y)
            memcpy(intercom_front + y * width * 2U, pixels + y * stride, width * 2U);
        intercom_descriptor = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                       .w = width, .h = height, .stride = width * 2U},
            .data_size = width * height * 2U, .data = intercom_front,
        };
        lv_image_set_src(intercom_image, &intercom_descriptor);
        unsigned available_height = intercom_has_gate ? H - 172 : H - 68;
        unsigned scale_x = W * 256U / width;
        unsigned scale_y = available_height * 256U / height;
        unsigned scale = scale_x < scale_y ? scale_x : scale_y;
        lv_image_set_scale(intercom_image, scale);
        lv_obj_align(intercom_image, LV_ALIGN_CENTER, 0,
                     intercom_has_gate ? -36 : 20);
        lv_obj_remove_flag(intercom_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(intercom_image);
        intercom_video_status[0] = 0;
    }
    bsp_display_unlock();
    return accepted;
}

void app_intercom_ui_status(const char *message, unsigned revision)
{
    if (bsp_display_lock(100) != ESP_OK)
        return;
    if (intercom_overlay && revision == intercom_revision)
        snprintf(intercom_video_status, sizeof(intercom_video_status), "%s", message);
    bsp_display_unlock();
}
#else
static bool intercom_prepare(void) { return false; }
static void intercom_render(void) {}
#endif
