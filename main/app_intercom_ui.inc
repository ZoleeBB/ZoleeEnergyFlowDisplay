/* Az app_ui.c része. A kaputelefon a felső rétegen él: a háttérben történő
 * téma-/hibaoldalváltás és a gesztus nem zárhatja be a csengetés közben. */
#if defined(ESP_PLATFORM) || defined(APP_TEMPLATE_DESKTOP)
#include "app_intercom.h"
#include "app_intercom_audio.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define INTERCOM_FRONT_SIZE (1280U * 720U * 2U)
static lv_obj_t *intercom_overlay, *intercom_image, *intercom_message, *intercom_gate;
static lv_obj_t *intercom_audio, *intercom_audio_message;
static uint8_t *intercom_front;
static uint8_t *intercom_prepared;
/* A dekóder a tartalék pufferbe ír LVGL-lock nélkül. A UI zárolva cseréli
 * fel a két puffert, így a rajzoló által használt kép soha nem módosul. */
static uint8_t *intercom_spare;
static bool intercom_frame_ready, intercom_frame_writing, intercom_frame_accepting;
static unsigned intercom_frame_revision, intercom_frame_width, intercom_frame_height;
static lv_image_dsc_t intercom_descriptor;
static app_intercom_snapshot_t intercom_next;
static unsigned intercom_revision;
static char intercom_video_status[LX_HOME_ERROR_SIZE];
/* A rövid háttérállapot nem veszhet el egy elfoglalt LVGL-lock miatt.
 * A videófeladat csak ebbe a postaládába ír; a UI saját lock alatt veszi át. */
static char intercom_pending_status[LX_HOME_ERROR_SIZE];
static unsigned intercom_pending_revision;
static bool intercom_pending;
#ifdef ESP_PLATFORM
static portMUX_TYPE intercom_status_lock = portMUX_INITIALIZER_UNLOCKED;
#define INTERCOM_STATUS_LOCK() portENTER_CRITICAL(&intercom_status_lock)
#define INTERCOM_STATUS_UNLOCK() portEXIT_CRITICAL(&intercom_status_lock)
#else
#define INTERCOM_STATUS_LOCK() ((void)0)
#define INTERCOM_STATUS_UNLOCK() ((void)0)
#endif
static int64_t intercom_last_press;
static bool intercom_background_suspended;

static void intercom_toggle_audio(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    app_intercom_audio_status_t audio;
    app_intercom_audio_snapshot(&audio);
    bool active = audio.state == APP_INTERCOM_AUDIO_CONNECTING ||
                  audio.state == APP_INTERCOM_AUDIO_ACTIVE;
    if (app_intercom_audio_request(!active, intercom_revision) != ESP_OK) {
        lv_label_set_text(intercom_audio_message,
                          tr("A hang nem indítható. Ellenőrizd a kapcsolatot.",
                             "Cannot start audio. Check the connection."));
    }
}

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
        !app_loxone_intercom_snapshot(&intercom_next)) {
        memset(&intercom_next, 0, sizeof(intercom_next));
    }
    /* A helyi nézetet a felépült hanghívás is nyitva tartja. */
    intercom_next.ringing = intercom_next.ringing || intercom_next.audio_active;
    if (intercom_next.ringing && !intercom_background_suspended) {
        if (bsp_display_lock(-1) != ESP_OK) {
            return true;
        }
        /* Először megszűnnek a háttér widgetjei, utána a képek és cache-ek.
         * A kiválasztott téma és a kért Otthon-oldal megmarad, csak a RAM-beli
         * pillanatkép ürül ki. A kamera még nem foglalhat előtte memóriát. */
        home_reset_theme();
        home_page = 0;
        template_loading_requested = true;
        template_waiting_for_load = true;
        intercom_background_suspended = true;
        bg_color_if_changed(s_screen, c(0x000000), 0);
        lv_obj_set_style_bg_grad_dir(s_screen, LV_GRAD_DIR_NONE, 0);
        bsp_display_unlock();
    }
    /* A dekóder/hang felszabadítása megelőzi a nagy téma visszatöltését.
     * A nézet a 0-s csengetésre azonnal eltűnik, itt már csak fekete háttér marad. */
    if (!intercom_next.ringing && intercom_background_suspended && !app_intercom_idle()) {
        return true;
    }
    if (!intercom_next.ringing) {
        intercom_background_suspended = false;
    }
    if (intercom_next.ringing && !intercom_front && !intercom_prepared)
    {
#ifdef ESP_PLATFORM
        intercom_prepared = heap_caps_malloc(INTERCOM_FRONT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        intercom_prepared = malloc(INTERCOM_FRONT_SIZE);
#endif
    }
    if (intercom_next.ringing) {
        if (!intercom_spare) {
#ifdef ESP_PLATFORM
            uint8_t *spare = heap_caps_malloc(INTERCOM_FRONT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            uint8_t *spare = malloc(INTERCOM_FRONT_SIZE);
#endif
            INTERCOM_STATUS_LOCK();
            intercom_spare = spare;
            INTERCOM_STATUS_UNLOCK();
        }
        INTERCOM_STATUS_LOCK();
        if (!intercom_frame_accepting || intercom_frame_revision != intercom_next.revision) {
            intercom_frame_ready = false;
            intercom_frame_revision = intercom_next.revision;
        }
        intercom_frame_accepting = true;
        INTERCOM_STATUS_UNLOCK();
    }
#ifdef ESP_PLATFORM
    if (intercom_next.ringing && intercom_background_suspended)
        app_intercom_poll();
#endif
    return intercom_next.ringing;
}

/* Ezeket a mutatókat kizárólag a runtime hozza létre/szabadítja fel.
 * Üresjáratban ezért LVGL-lock kérése nélkül is kihagyható a takarítás. */
static bool intercom_needs_cleanup(void)
{
    return intercom_overlay || intercom_front || intercom_prepared || intercom_spare;
}

/* Kizárólag a UI hívja, a szokásos LVGL-lock birtokában. */
static void intercom_present_frame(void)
{
    if (!intercom_front) return;
    INTERCOM_STATUS_LOCK();
    bool ready = intercom_frame_ready && !intercom_frame_writing &&
                 intercom_frame_revision == intercom_revision;
    INTERCOM_STATUS_UNLOCK();
    if (!ready) return;
    lv_image_cache_drop(&intercom_descriptor);
    INTERCOM_STATUS_LOCK();
    /* A producer az előző vizsgálat óta új képet kezdhetett írni. */
    if (!intercom_frame_ready || intercom_frame_writing || intercom_frame_revision != intercom_revision) {
        INTERCOM_STATUS_UNLOCK();
        return;
    }
    uint8_t *old_front = intercom_front;
    intercom_front = intercom_spare;
    intercom_spare = old_front;
    unsigned width = intercom_frame_width, height = intercom_frame_height;
    intercom_frame_ready = false;
    INTERCOM_STATUS_UNLOCK();
    intercom_descriptor = (lv_image_dsc_t){
        .header = {.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_RGB565,
                   .w=width, .h=height, .stride=width * 2U},
        .data_size=width * height * 2U, .data=intercom_front,
    };
    lv_image_set_src(intercom_image, &intercom_descriptor);
    unsigned scale_x = W * 256U / width, scale_y = (H - 204) * 256U / height;
    lv_image_set_scale(intercom_image, scale_x < scale_y ? scale_x : scale_y);
    lv_obj_align(intercom_image, LV_ALIGN_CENTER, 0, -24);
    lv_obj_remove_flag(intercom_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(intercom_image);
    intercom_video_status[0] = 0;
    INTERCOM_STATUS_LOCK();
    if (intercom_pending_revision == intercom_revision) intercom_pending = false;
    INTERCOM_STATUS_UNLOCK();
}

static void intercom_render(void)
{
    if (!intercom_next.ringing)
    {
        /* Üresjáratban a háttér képcache-éhez és az audiovezérléshez sem nyúlunk. */
        if (!intercom_overlay && !intercom_front && !intercom_prepared && !intercom_spare)
            return;
        INTERCOM_STATUS_LOCK();
        intercom_frame_accepting = intercom_frame_ready = false;
        bool writing = intercom_frame_writing;
        INTERCOM_STATUS_UNLOCK();
        (void)app_intercom_audio_request(false, intercom_revision);
        if (intercom_overlay)
        {
            lv_obj_delete(intercom_overlay);
            intercom_overlay = intercom_image = intercom_message = intercom_gate = NULL;
            intercom_audio = intercom_audio_message = NULL;
        }
        lv_image_cache_drop(&intercom_descriptor);
        /* A producer LVGL-lock nélkül másolhat: csak a teljes leállása után
         * szabad a két képpuffert felszabadítani. A háttér is addig vár. */
        if (writing || !app_intercom_idle()) return;
        free(intercom_front);
        free(intercom_prepared);
        free(intercom_spare);
        intercom_spare = NULL;
        intercom_front = intercom_prepared = NULL;
        memset(&intercom_descriptor, 0, sizeof(intercom_descriptor));
        return;
    }
    if (!intercom_overlay)
    {
        intercom_revision = intercom_next.revision;
        intercom_last_press = 0;
        /* Gyors újracsengésnél a régi worker bontása még tarthat, a UI-puffer
         * ilyenkor megmaradt. Nem írhatjuk felül NULL előkészített pufferrel. */
        if (!intercom_front) {
            intercom_front = intercom_prepared;
            intercom_prepared = NULL;
        }
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
        intercom_audio_message = lbl(intercom_overlay, "", 24, 54, APP_FONT_20, 0xffffff);
        lv_obj_set_width(intercom_audio_message, W - 48);
        lv_label_set_long_mode(intercom_audio_message, LV_LABEL_LONG_WRAP);
        intercom_audio = lv_button_create(intercom_overlay);
        lv_obj_set_size(intercom_audio, 480, 92);
        lv_obj_set_style_bg_color(intercom_audio, c(0x246696), 0);
        lv_obj_set_style_radius(intercom_audio, 20, 0);
        lv_obj_set_style_text_font(intercom_audio, APP_FONT_28, 0);
        lv_obj_set_style_text_color(intercom_audio, lv_color_white(), 0);
        lv_obj_remove_flag(intercom_audio, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(intercom_audio, intercom_toggle_audio, LV_EVENT_CLICKED, NULL);
        lv_obj_t *audio_title = lv_label_create(intercom_audio);
        lv_obj_set_width(audio_title, 432);
        lv_label_set_long_mode(audio_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(audio_title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(audio_title);
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
        (void)app_intercom_audio_request(false, intercom_revision);
        intercom_revision = intercom_next.revision;
        lv_obj_add_flag(intercom_image, LV_OBJ_FLAG_HIDDEN);
        strcpy(intercom_video_status, tr("Kapcsolódás a kamerához...", "Connecting to camera..."));
    }
    INTERCOM_STATUS_LOCK();
    if (intercom_pending) {
        if (intercom_pending_revision == intercom_revision) {
            memcpy(intercom_video_status, intercom_pending_status, sizeof(intercom_video_status));
        }
        intercom_pending = false;
    }
    INTERCOM_STATUS_UNLOCK();
    intercom_present_frame();
    bool gate = intercom_next.config.gate.control_uuid[0] != 0;
    lv_obj_align(intercom_gate, LV_ALIGN_BOTTOM_MID, -250, -20);
    lv_obj_align(intercom_audio, LV_ALIGN_BOTTOM_MID, gate ? 250 : 0, -20);
    app_intercom_audio_status_t audio;
    app_intercom_audio_snapshot(&audio);
    bool talking = audio.state == APP_INTERCOM_AUDIO_ACTIVE;
    bool connecting = audio.state == APP_INTERCOM_AUDIO_CONNECTING;
    bool stopping = audio.state == APP_INTERCOM_AUDIO_STOPPING;
    lv_label_set_text(lv_obj_get_child(intercom_audio, 0),
                      talking ? tr("Beszélgetés vége", "End conversation") :
                      connecting ? tr("Kapcsolódás megszakítása", "Cancel connection") :
                      stopping ? tr("Hang leállítása...", "Stopping audio...") :
                      tr("Beszélgetés", "Start conversation"));
    lv_obj_set_style_bg_color(intercom_audio, c(talking || connecting ? 0xa53030 : 0x246696), 0);
    if (intercom_next.audio_elsewhere || stopping || (!intercom_next.connected && !talking && !connecting)) {
        lv_obj_add_state(intercom_audio, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(intercom_audio, LV_STATE_DISABLED);
    }
    lv_label_set_text(intercom_audio_message,
                      intercom_next.audio_elsewhere ? tr("Beszélgetés másik kijelzőn / várakozás a Masterre", "Conversation on another display / waiting for Master") :
                      audio.revision == intercom_revision && !talking ? audio.message : "");
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
    const char *message = !intercom_front || !intercom_spare ? tr("Nincs elég memória a kameraképhez.", "Not enough memory for video.") :
        !intercom_next.connected ? tr("A Loxone-kapcsolat megszakadt. A csengetés végére várunk...",
                                    "Loxone disconnected. Waiting for ringing to end...") :
        intercom_last_press && esp_timer_get_time() - intercom_last_press < 3000000LL &&
            intercom_next.status[0] ? intercom_next.status : intercom_video_status;
    lv_label_set_text(intercom_message, message);
    /* A kapunyitó mindig a videó fölött marad; nincs bezáró gomb vagy időzítő. */
    lv_obj_move_foreground(intercom_message);
    lv_obj_move_foreground(intercom_gate);
    lv_obj_move_foreground(intercom_audio);
    lv_obj_move_foreground(intercom_audio_message);
}

bool app_intercom_ui_frame(const uint8_t *pixels, unsigned width, unsigned height,
                           unsigned stride, unsigned revision)
{
    if (!pixels || !width || !height || width > 1280 || height > 720 || stride < width * 2U)
        return false;
    INTERCOM_STATUS_LOCK();
    bool accepted = intercom_frame_accepting && intercom_spare &&
                    !intercom_frame_writing && revision == intercom_frame_revision;
    uint8_t *destination = accepted ? intercom_spare : NULL;
    if (accepted) {
        intercom_frame_writing = true;
        intercom_frame_ready = false;
    }
    INTERCOM_STATUS_UNLOCK();
    if (!accepted) return false;
    for (unsigned y = 0; y < height; ++y)
        memcpy(destination + y * width * 2U, pixels + y * stride, width * 2U);
    INTERCOM_STATUS_LOCK();
    accepted = intercom_frame_accepting && revision == intercom_frame_revision;
    if (accepted) {
        intercom_frame_width = width;
        intercom_frame_height = height;
        intercom_frame_ready = true;
    }
    intercom_frame_writing = false;
    INTERCOM_STATUS_UNLOCK();
    return accepted;
}

void app_intercom_ui_status(const char *message, unsigned revision)
{
    INTERCOM_STATUS_LOCK();
    snprintf(intercom_pending_status, sizeof(intercom_pending_status), "%s", message ? message : "");
    intercom_pending_revision = revision;
    intercom_pending = true;
    INTERCOM_STATUS_UNLOCK();
}
#undef INTERCOM_STATUS_LOCK
#undef INTERCOM_STATUS_UNLOCK

#else
static bool intercom_prepare(void) { return false; }
static void intercom_render(void) {}
static bool intercom_needs_cleanup(void) { return false; }
#endif
