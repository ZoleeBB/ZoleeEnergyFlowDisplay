/* SD-lejátszóburkolat, három elkülönített érintési cél és egyetlen modal.
 * A képek referenciája a widgetcsere után szabadul fel; minden hálózati munka
 * külön workerben fut. Üresjáratban a szöveg/kép értéke nem íródik újra. */
#include "app_loxone_audio_service.h"
#include "app_loxone_audio_media.h"

#define AUDIO_UI_ROWS 8U
#define AUDIO_UI_VISIBLE_ROWS 6U

typedef struct
{
    lv_obj_t *object;
    lx_audio_image_t *owned;
    lv_image_dsc_t descriptor;
    const lv_image_dsc_t *fallback;
    unsigned size, display_size;
} audio_ui_picture_t;

typedef struct
{
    lv_obj_t *play, *volume, *title, *status, *screen;
    audio_ui_picture_t cover;
    char title_cache[LX_AUDIO_TEXT_SIZE], status_cache[96];
} audio_ui_card_t;

static TEMPLATE_RAM audio_ui_card_t audio_cards[LX_HOME_MAX_CARDS];
static TEMPLATE_RAM lx_audio_summary_t audio_ui_summary;
static lv_obj_t *audio_overlay, *audio_panel, *audio_volume_slider, *audio_volume_label;
static lv_obj_t *audio_track_title, *audio_track_artist, *audio_track_source, *audio_status;
static lv_obj_t *audio_transport_buttons[3], *audio_volume_buttons[2];
static lv_obj_t *audio_favorites_prev, *audio_favorites_next, *audio_favorites_label;
static lv_obj_t *audio_favorites_rows[AUDIO_UI_ROWS];
static unsigned audio_favorites_ids[AUDIO_UI_ROWS];
static audio_ui_picture_t audio_detail_cover;
static lx_audio_favorites_t *audio_favorites_snapshot;
static char audio_card_id[LX_HOME_ID_SIZE];
static unsigned audio_ui_revision, audio_ui_page, audio_ui_index, audio_ui_generation;
static unsigned audio_favorites_offset, audio_favorites_generation;
static unsigned audio_favorites_backward_end;
static bool audio_detail, audio_dragging, audio_syncing, audio_submit_error;
static TEMPLATE_RAM char audio_modal_text[3 + AUDIO_UI_ROWS][LX_AUDIO_TEXT_SIZE];

/* DOTS esetén az LVGL a saját szövegét rövidíti; a teljes forrás cache-e
 * akadályozza meg a változatlan hosszú cím 200 ms-onkénti újraírását. */
static void audio_cached_text(lv_obj_t *label, char *cache, size_t capacity, const char *text)
{
    if (label && strcmp(cache, text))
    {
        size_t length = strlen(text);
        if (length >= capacity)
        {
            length = capacity - 1;
            while (length && ((unsigned char)text[length] & 0xc0U) == 0x80U)
                --length;
        }
        memcpy(cache, text, length);
        cache[length] = 0;
        lv_label_set_text(label, cache);
    }
}

static const lv_image_dsc_t *home_audio_skin(void);
static void audio_ui_update(void);

static bool audio_ui_is_open(void)
{
    return audio_overlay != NULL;
}

static void audio_picture_release(audio_ui_picture_t *picture)
{
    if (picture->owned)
    {
        lv_image_cache_drop(&picture->descriptor);
        app_loxone_audio_media_release(picture->owned);
    }
    memset(picture, 0, sizeof(*picture));
}

static void audio_ui_reset(void)
{
    if (audio_card_id[0])
        app_loxone_audio_cancel_volume(audio_card_id, audio_ui_revision);
    if (++audio_ui_generation == 0)
        ++audio_ui_generation;
    if (audio_overlay)
        lv_obj_delete(audio_overlay);
    audio_overlay = audio_panel = audio_volume_slider = audio_volume_label = NULL;
    audio_track_title = audio_track_artist = audio_track_source = audio_status = NULL;
    audio_favorites_prev = audio_favorites_next = audio_favorites_label = NULL;
    memset(audio_transport_buttons, 0, sizeof(audio_transport_buttons));
    memset(audio_volume_buttons, 0, sizeof(audio_volume_buttons));
    memset(audio_favorites_rows, 0, sizeof(audio_favorites_rows));
    memset(audio_favorites_ids, 0, sizeof(audio_favorites_ids));
    audio_picture_release(&audio_detail_cover);
    free(audio_favorites_snapshot);
    audio_favorites_snapshot = NULL;
    audio_card_id[0] = 0;
    audio_detail = audio_dragging = audio_syncing = audio_submit_error = false;
    audio_favorites_offset = audio_favorites_generation = 0;
    audio_favorites_backward_end = 0;
    memset(audio_modal_text, 0, sizeof(audio_modal_text));
    app_loxone_audio_detail_close();
}

static void audio_ui_suspend(void)
{
    audio_ui_reset();
    /* Kapcsolatvesztéskor a kirajzolt kártyafa megmarad. A modal és a
     * hálózati munka érvénytelen, de a widgetek és a még látható képek
     * referenciái kellenek a következő offline/újracsatlakozott frissítéshez. */
    app_loxone_audio_hide();
}

static void audio_ui_dispose(void)
{
    /* Kizárólag a tényleges clear() hívja, a suspend után. A forrásokat
     * a widgettörlés és a workerrel megosztott pixelek elengedése előtt
     * leválasztjuk; a következő kártyafa új referenciákat épít. */
    for (unsigned i = 0; i < LX_HOME_MAX_CARDS; ++i)
    {
        if (audio_cards[i].cover.object)
            lv_image_set_src(audio_cards[i].cover.object, NULL);
        audio_picture_release(&audio_cards[i].cover);
    }
    memset(audio_cards, 0, sizeof(audio_cards));
}

/* A beépített Noto betűkészletben nincs U+2026 és U+2212: a saját
 * feliratainkhoz a meglévő pontot és kötőjelet használjuk. */
static const char *audio_connection_text(const lx_audio_summary_t *summary)
{
    if (summary->status == LX_AUDIO_AUTH_FAILED)
        return tr("Audio: sikertelen belépés", "Audio authentication failed");
    if (summary->status == LX_AUDIO_CONNECTING)
        return tr("Audio kapcsolódás...", "Connecting audio...");
    return tr("Audio-kapcsolat nem érhető el", "Audio connection unavailable");
}

static const char *audio_status_text(const lx_audio_summary_t *summary)
{
    if (!summary->available)
        return tr("Nincs adat", "No data");
    /* A Miniserver lejátszási állapota a natív Audio-kapcsolat kiesése
     * mellett is megmaradhat. Ez nem fedheti el az adatkapcsolat hibáját. */
    if (!summary->native_ready || summary->status == LX_AUDIO_AUTH_FAILED ||
        summary->status == LX_AUDIO_UNAVAILABLE || summary->status == LX_AUDIO_CONNECTING)
        return audio_connection_text(summary);
    if (!summary->controllable)
        return tr("Vezérlés nem elérhető", "Control unavailable");
    if (summary->volume_failed)
        return tr("Hangerőállítás sikertelen", "Volume change failed");
    if (summary->status == LX_AUDIO_REQUEST_FAILED)
        return tr("A kérés nem fogadható el", "Request not accepted");
    if (summary->volume_pending)
        return tr("Hangerő küldése...", "Setting volume...");
    if (summary->track.mode == LX_AUDIO_MODE_PLAY)
        return tr("Lejátszás", "Playing");
    if (summary->track.mode == LX_AUDIO_MODE_PAUSE)
        return tr("Szünet", "Paused");
    if (summary->track.mode == LX_AUDIO_MODE_STOP)
        return tr("Leállítva", "Stopped");
    return tr("Állapotra vár", "Waiting for status");
}

static const char *audio_title_text(const lx_audio_summary_t *summary, bool detail)
{
    if (summary->track.title[0])
        return summary->track.title;
    if (summary->track.station[0])
        return summary->track.station;
    /* Az üres metaadat nem bizonyítja, hogy nincs lejátszott zeneszám. */
    return detail ? tr("Zeneszámadat nem érhető el", "Track information unavailable") :
        tr("Nincs zeneszámadat", "No track data");
}

static bool audio_card_current(unsigned index)
{
    return !s_pair_pad && !loxone_display_is_held() && home_connected && home_page == 2 &&
        home_request_get() == 2 + (int)home_card_page && index >= home_first_card &&
        index < home_first_card + home_card_count && index < home_config.card_count &&
        home_config.cards[index].type == LX_HOME_CARD_AUDIO;
}

static bool audio_modal_current(void)
{
    return audio_overlay && audio_ui_revision == home_revision && audio_ui_page == home_card_page &&
        audio_card_current(audio_ui_index) &&
        !strcmp(audio_card_id, home_config.cards[audio_ui_index].id);
}

static bool audio_modal_event(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    return (unsigned)(uintptr_t)lv_event_get_user_data(event) == audio_ui_generation &&
        audio_modal_current();
}

static void audio_symbol(lv_obj_t *button, const char *symbol)
{
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (lv_obj_get_style_text_font(label, 0) != LV_FONT_DEFAULT)
        lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    label_text_if_changed(label, symbol);
}

static lv_obj_t *audio_button(lv_obj_t *parent, const char *symbol, int x, int y, int w, int h)
{
    lv_obj_t *button = pairing_button(parent, "", x, y, w, h);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_style_radius(button, 12, 0);
    bg_color_if_changed(button, c(0x007D7D), 0);
    audio_symbol(button, symbol);
    return button;
}

static void audio_picture_update(audio_ui_picture_t *picture, unsigned slot,
                                 const lx_audio_summary_t *summary)
{
    if (!picture->object)
        return;
    app_loxone_audio_media_request(slot, summary->generation, summary->track.cover,
        summary->track.version, picture->size);
    lx_audio_image_t *next = app_loxone_audio_media_acquire(slot, summary->generation);
    if (next == picture->owned)
    {
        app_loxone_audio_media_release(next);
        return;
    }
    lx_audio_image_t *old = picture->owned;
    if (old)
        lv_image_cache_drop(&picture->descriptor);
    picture->owned = next;
    if (next)
    {
        picture->descriptor = (lv_image_dsc_t){
            .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                .w = next->width, .h = next->height, .stride = next->width * 2},
            .data_size = next->width * next->height * 2, .data = (const uint8_t *)next->pixels,
        };
        lv_image_set_src(picture->object, &picture->descriptor);
        unsigned largest = next->width > next->height ? next->width : next->height;
        lv_image_set_scale(picture->object, picture->display_size * 256 / largest);
        lv_obj_center(picture->object);
    }
    else if (picture->fallback)
    {
        lv_image_set_src(picture->object, picture->fallback);
        lv_image_set_scale(picture->object, 256);
        lv_obj_center(picture->object);
    }
    else
        lv_image_set_src(picture->object, NULL);
    app_loxone_audio_media_release(old);
}

static void audio_ui_close_event(lv_event_t *event)
{
    if (lv_event_get_target(event) != lv_event_get_current_target(event))
        return;
    if (!audio_modal_event(event))
        return;
    lv_indev_t *input = lv_indev_active();
    if (input)
        lv_indev_wait_release(input);
    audio_ui_reset();
}

static void audio_ui_transport_event(lv_event_t *event)
{
    if (!audio_modal_event(event))
        return;
    lv_obj_t *object = lv_event_get_current_target(event);
    lx_audio_action_t action;
    if (object == audio_transport_buttons[0])
        action = LX_AUDIO_PREVIOUS;
    else if (object == audio_transport_buttons[2])
        action = LX_AUDIO_NEXT;
    else if (object == audio_volume_buttons[0])
        action = LX_AUDIO_VOLUME_DOWN;
    else if (object == audio_volume_buttons[1])
        action = LX_AUDIO_VOLUME_UP;
    else
    {
        if (!app_loxone_audio_summary(audio_card_id, audio_ui_revision, &audio_ui_summary) ||
            audio_ui_summary.track.mode == LX_AUDIO_MODE_UNKNOWN)
            return;
        action = audio_ui_summary.track.mode == LX_AUDIO_MODE_PLAY ? LX_AUDIO_PAUSE : LX_AUDIO_PLAY;
    }
    audio_submit_error = app_loxone_audio_command(audio_card_id, audio_ui_revision, action, 0, true) != ESP_OK;
    audio_ui_update();
}

static void audio_ui_volume_event(lv_event_t *event)
{
    if (audio_syncing || !audio_modal_event(event))
        return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED)
        audio_dragging = true;
    bool final = code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST;
    if (code == LV_EVENT_VALUE_CHANGED || final)
        audio_submit_error = app_loxone_audio_command(audio_card_id, audio_ui_revision,
            LX_AUDIO_VOLUME, (unsigned)lv_slider_get_value(audio_volume_slider), final) != ESP_OK;
    if (final)
        audio_dragging = false;
    audio_ui_update();
}

static void audio_ui_favorite_event(lv_event_t *event)
{
    if (!audio_modal_event(event))
        return;
    lv_obj_t *target = lv_event_get_current_target(event);
    for (unsigned i = 0; i < AUDIO_UI_ROWS; ++i)
        if (target == audio_favorites_rows[i] && audio_favorites_ids[i])
        {
            audio_submit_error = app_loxone_audio_favorite(audio_card_id, audio_ui_revision,
                audio_favorites_ids[i], audio_favorites_generation) != ESP_OK;
            break;
        }
    audio_ui_update();
}

static void audio_ui_favorites_page(lv_event_t *event)
{
    if (!audio_modal_event(event) || !audio_favorites_snapshot || !audio_favorites_snapshot->ready)
        return;
    lx_audio_favorites_t *f = audio_favorites_snapshot;
    if (lv_event_get_current_target(event) == audio_favorites_next)
    {
        audio_favorites_backward_end = 0;
        if (audio_favorites_offset + AUDIO_UI_VISIBLE_ROWS < f->count)
            audio_favorites_offset += AUDIO_UI_VISIBLE_ROWS;
        else if (f->start + f->count < f->total)
        {
            audio_favorites_offset = 0;
            app_loxone_audio_favorites_request(audio_card_id, audio_ui_revision,
                f->start + f->count);
        }
    }
    else if (audio_favorites_offset >= AUDIO_UI_VISIBLE_ROWS)
        audio_favorites_offset -= AUDIO_UI_VISIBLE_ROWS;
    else if (f->start)
    {
        /* A szerver rövid oldalt is küldhet. Az előző hat elem abszolút
         * helye a hálózati 32-es lapmérettől és a korábbi válaszoktól független. */
        audio_favorites_backward_end = f->start;
        audio_favorites_offset = 0;
        audio_submit_error = app_loxone_audio_favorites_request(audio_card_id, audio_ui_revision,
            f->start > AUDIO_UI_VISIBLE_ROWS ? f->start - AUDIO_UI_VISIBLE_ROWS : 0) != ESP_OK;
        if (audio_submit_error)
            audio_favorites_backward_end = 0;
    }
    audio_ui_update();
}

static void audio_ui_update(void)
{
    if (!audio_overlay)
        return;
    if (!audio_modal_current() || !app_loxone_audio_summary(audio_card_id, audio_ui_revision, &audio_ui_summary))
    {
        audio_ui_reset();
        return;
    }
    const lx_audio_summary_t *summary = &audio_ui_summary;
    bool allowed = summary->controllable;
    if (audio_volume_slider)
    {
        audio_syncing = true;
        unsigned maximum = summary->volume_max ? summary->volume_max : 1;
        if ((unsigned)lv_slider_get_max_value(audio_volume_slider) != maximum)
            lv_slider_set_range(audio_volume_slider, 0, (int)maximum);
        if (!audio_dragging && (summary->volume_pending || summary->track.volume_known))
        {
            unsigned value = summary->volume_pending ? summary->desired_volume : summary->track.volume;
            if ((unsigned)lv_slider_get_value(audio_volume_slider) != value)
                lv_slider_set_value(audio_volume_slider, (int)value, LV_ANIM_OFF);
        }
        audio_syncing = false;
        light_ui_hidden(audio_volume_slider, summary->volume_up_down);
        light_ui_enable(audio_volume_slider, allowed && !summary->volume_up_down && summary->volume_max > 0);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        light_ui_hidden(audio_volume_buttons[i], !summary->volume_up_down);
        light_ui_enable(audio_volume_buttons[i], allowed);
    }
    char text[80];
    if (summary->volume_up_down)
        snprintf(text, sizeof(text), "%s", tr("Külső hangerő: - / +", "External volume: - / +"));
    else if (audio_dragging || summary->volume_pending)
        snprintf(text, sizeof(text), "%s: %u%%", tr("Célhangerő", "Target volume"),
            audio_dragging && audio_volume_slider ? (unsigned)lv_slider_get_value(audio_volume_slider) : summary->desired_volume);
    else if (summary->track.volume_known)
        snprintf(text, sizeof(text), "%s: %u%%", tr("Hangerő", "Volume"), summary->track.volume);
    else
        snprintf(text, sizeof(text), "%s", tr("Hangerő: nincs adat", "Volume: no data"));
    label_text_if_changed(audio_volume_label, text);
    bool request_error = audio_submit_error && summary->available && summary->controllable && summary->native_ready;
    label_text_if_changed(audio_status, request_error ? tr("A kérés nem fogadható el.", "Request not accepted.") :
        audio_status_text(summary));
    if (!audio_detail)
        return;
    audio_cached_text(audio_track_title, audio_modal_text[0], LX_AUDIO_TEXT_SIZE, audio_title_text(summary, true));
    audio_cached_text(audio_track_artist, audio_modal_text[1], LX_AUDIO_TEXT_SIZE, summary->track.artist);
    audio_cached_text(audio_track_source, audio_modal_text[2], LX_AUDIO_TEXT_SIZE,
        summary->track.title[0] && summary->track.station[0] ? summary->track.station : summary->track.album);
    audio_picture_update(&audio_detail_cover, LX_AUDIO_MEDIA_SLOTS - 1, summary);
    for (unsigned i = 0; i < 3; ++i)
        light_ui_enable(audio_transport_buttons[i], allowed && !home_command_wait &&
            (i != 1 || summary->track.mode != LX_AUDIO_MODE_UNKNOWN));
    audio_symbol(audio_transport_buttons[1], summary->track.mode == LX_AUDIO_MODE_PLAY ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (!audio_favorites_snapshot || !app_loxone_audio_favorites(audio_card_id, audio_ui_revision, audio_favorites_snapshot))
        return;
    lx_audio_favorites_t *favorites = audio_favorites_snapshot;
    if (favorites->failed)
        audio_favorites_backward_end = 0;
    if (favorites->ready && audio_favorites_backward_end)
    {
        /* Ha még hat elem sem érkezett, a korábbi végpont felé szűkítünk.
         * A kezdés minden újrakéréskor nő: legfeljebb hat aszinkron lépés,
         * nincs UI-várakozás és visszalépéskor sem marad ki egyetlen elem. */
        unsigned end = audio_favorites_backward_end;
        if (favorites->count && favorites->start + favorites->count < end)
        {
            unsigned count = favorites->count < AUDIO_UI_VISIBLE_ROWS ? favorites->count : AUDIO_UI_VISIBLE_ROWS;
            unsigned start = end - count;
            audio_submit_error = app_loxone_audio_favorites_request(audio_card_id, audio_ui_revision, start) != ESP_OK;
            if (!audio_submit_error)
                favorites->ready = false;
            else
                audio_favorites_backward_end = 0;
        }
        else
            audio_favorites_backward_end = 0;
    }
    audio_favorites_generation = favorites->generation;
    if (favorites->ready && audio_favorites_offset >= favorites->count)
        audio_favorites_offset = 0;
    for (unsigned i = 0; i < AUDIO_UI_ROWS; ++i)
    {
        unsigned index = audio_favorites_offset + i;
        bool visible = favorites->ready && index < favorites->count && i < AUDIO_UI_VISIBLE_ROWS;
        audio_favorites_ids[i] = visible ? favorites->items[index].id : 0;
        light_ui_hidden(audio_favorites_rows[i], !visible);
        light_ui_enable(audio_favorites_rows[i], visible && allowed && summary->native_ready);
        if (visible)
            audio_cached_text(lv_obj_get_child(audio_favorites_rows[i], 0), audio_modal_text[3 + i],
                LX_AUDIO_TEXT_SIZE, favorites->items[index].name);
    }
    if (favorites->failed)
        snprintf(text, sizeof(text), "%s", tr("A kedvenclista nem érhető el.", "Favorites unavailable."));
    else if (!summary->native_ready)
        snprintf(text, sizeof(text), "%s", audio_connection_text(summary));
    else if (!favorites->ready)
        snprintf(text, sizeof(text), "%s", tr("Kedvencek betöltése...", "Loading favorites..."));
    else if (!favorites->total)
        snprintf(text, sizeof(text), "%s", tr("Nincs beállított kedvenc.", "No configured favorites."));
    else
    {
        unsigned last = audio_favorites_offset + AUDIO_UI_VISIBLE_ROWS;
        if (last > favorites->count)
            last = favorites->count;
        snprintf(text, sizeof(text), "%u–%u / %u", favorites->start + audio_favorites_offset + 1,
            favorites->start + last, favorites->total);
    }
    label_text_if_changed(audio_favorites_label, text);
    light_ui_enable(audio_favorites_prev, summary->native_ready && favorites->ready && (favorites->start || audio_favorites_offset));
    light_ui_enable(audio_favorites_next, summary->native_ready && favorites->ready &&
        (audio_favorites_offset + AUDIO_UI_VISIBLE_ROWS < favorites->count ||
         favorites->start + favorites->count < favorites->total));
}

static void audio_ui_open(unsigned index, bool detail)
{
    if (!audio_card_current(index) || lighting_ui_is_open() || shading_ui_is_open() ||
        controls_ui_is_open() || audio_overlay)
        return;
    if (!app_loxone_audio_summary(home_config.cards[index].id, home_revision, &audio_ui_summary))
        return;
    audio_ui_reset();
    audio_ui_revision = home_revision;
    audio_ui_page = home_card_page;
    audio_ui_index = index;
    audio_detail = detail;
    snprintf(audio_card_id, sizeof(audio_card_id), "%s", home_config.cards[index].id);
    void *generation = (void *)(uintptr_t)audio_ui_generation;
    audio_overlay = shape(s_screen, 0, 0, 1280, 800, 0x000000, 0);
    bg_opa_if_changed(audio_overlay, 150, 0);
    lv_obj_remove_flag(audio_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(audio_overlay, audio_ui_close_event, LV_EVENT_CLICKED, generation);
    int width = detail ? 900 : 440, height = detail ? 650 : 208;
    audio_panel = shape(audio_overlay, (1280 - width) / 2, (800 - height) / 2, width, height,
        home_night ? 0x14272D : 0xFBFDF9, 24);
    lv_obj_remove_flag(audio_panel, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lbl(audio_panel, home_config.cards[index].name, 24, 16, APP_FONT_22, light_ui_foreground());
    lv_obj_set_size(title, width - 110, lv_font_get_line_height(APP_FONT_22));
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *close = pairing_button(audio_panel, "X", width - 66, 8, 50, 48);
    lv_obj_remove_flag(close, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(close, audio_ui_close_event, LV_EVENT_CLICKED, generation);
    int slider_x = detail ? 306 : 44, slider_y = detail ? 274 : 108;
    int slider_width = detail ? 512 : 352;
    audio_volume_label = lbl(audio_panel, "", slider_x - 8, slider_y - 38, APP_FONT_20, light_ui_foreground());
    audio_volume_slider = lv_slider_create(audio_panel);
    lv_obj_set_pos(audio_volume_slider, slider_x, slider_y);
    lv_obj_set_size(audio_volume_slider, slider_width, 14);
    lv_slider_set_range(audio_volume_slider, 0, 100);
    lv_obj_set_style_bg_color(audio_volume_slider, c(0x008C83), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(audio_volume_slider, c(0x42CFC0), LV_PART_KNOB);
    lv_obj_set_style_pad_all(audio_volume_slider, 10, LV_PART_KNOB);
    lv_obj_set_ext_click_area(audio_volume_slider, 18);
    lv_obj_remove_flag(audio_volume_slider, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(audio_volume_slider, audio_ui_volume_event, LV_EVENT_PRESSED, generation);
    lv_obj_add_event_cb(audio_volume_slider, audio_ui_volume_event, LV_EVENT_VALUE_CHANGED, generation);
    lv_obj_add_event_cb(audio_volume_slider, audio_ui_volume_event, LV_EVENT_RELEASED, generation);
    lv_obj_add_event_cb(audio_volume_slider, audio_ui_volume_event, LV_EVENT_PRESS_LOST, generation);
    for (unsigned i = 0; i < 2; ++i)
    {
        audio_volume_buttons[i] = pairing_button(audio_panel, i ? "+" : "-", slider_x + i * 130,
            slider_y - 8, 112, 44);
        lv_obj_remove_flag(audio_volume_buttons[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(audio_volume_buttons[i], audio_ui_transport_event, LV_EVENT_CLICKED, generation);
    }
    audio_status = lbl(audio_panel, "", 24, detail ? 620 : 170, APP_FONT_14, light_ui_muted());
    lv_obj_set_width(audio_status, detail ? 480 : 392);
    lv_label_set_long_mode(audio_status, LV_LABEL_LONG_MODE_DOTS);
    if (detail)
    {
        audio_favorites_snapshot = light_ui_alloc(1, sizeof(*audio_favorites_snapshot));
        lv_obj_t *screen = shape(audio_panel, 28, 72, 224, 224, 0x20343A, 12);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        audio_detail_cover.object = lv_image_create(screen);
        audio_detail_cover.size = 256;
        audio_detail_cover.display_size = 224;
        int icon = home_icon_index(LX_AUDIO_ICON_ID);
        audio_detail_cover.fallback = icon >= 0 ? &home_images[icon] : NULL;
        if (audio_detail_cover.fallback)
            lv_image_set_src(audio_detail_cover.object, audio_detail_cover.fallback);
        lv_obj_center(audio_detail_cover.object);
        audio_track_title = light_ui_label(audio_panel, "", 280, 78, 586);
        audio_track_artist = light_ui_label(audio_panel, "", 280, 114, 586);
        audio_track_source = light_ui_label(audio_panel, "", 280, 148, 586);
        const char *symbols[] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
        for (unsigned i = 0; i < 3; ++i)
        {
            audio_transport_buttons[i] = audio_button(audio_panel, symbols[i], 294 + i * 184, 180, 164, 48);
            lv_obj_add_event_cb(audio_transport_buttons[i], audio_ui_transport_event, LV_EVENT_CLICKED, generation);
        }
        lbl(audio_panel, tr("Kedvencek", "Favorites"), 28, 316, APP_FONT_22, light_ui_foreground());
        for (unsigned i = 0; i < AUDIO_UI_ROWS; ++i)
        {
            audio_favorites_rows[i] = pairing_button(audio_panel, "", 28, 350 + (i % AUDIO_UI_VISIBLE_ROWS) * 40, 844, 36);
            lv_obj_t *label = lv_obj_get_child(audio_favorites_rows[i], 0);
            lv_obj_set_style_text_font(label, APP_FONT_16, 0);
            lv_obj_set_width(label, 812);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_remove_flag(audio_favorites_rows[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_add_flag(audio_favorites_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_event_cb(audio_favorites_rows[i], audio_ui_favorite_event, LV_EVENT_CLICKED, generation);
        }
        audio_favorites_prev = pairing_button(audio_panel, tr("Előző", "Previous"), 580, 602, 138, 34);
        audio_favorites_next = pairing_button(audio_panel, tr("Következő", "Next"), 734, 602, 138, 34);
        lv_obj_remove_flag(audio_favorites_prev, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_remove_flag(audio_favorites_next, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(audio_favorites_prev, audio_ui_favorites_page, LV_EVENT_CLICKED, generation);
        lv_obj_add_event_cb(audio_favorites_next, audio_ui_favorites_page, LV_EVENT_CLICKED, generation);
        audio_favorites_label = lbl(audio_panel, "", 220, 320, APP_FONT_16, light_ui_muted());
        lv_obj_set_width(audio_favorites_label, 640);
        if (audio_favorites_snapshot)
            app_loxone_audio_favorites_request(audio_card_id, audio_ui_revision, 0);
    }
    audio_ui_update();
}

static void audio_card_event(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    unsigned encoded = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned index = encoded / 3, action = encoded % 3;
    if (!audio_card_current(index) || audio_ui_is_open() || lighting_ui_is_open() ||
        shading_ui_is_open() || controls_ui_is_open())
        return;
    if (action < 2)
        audio_ui_open(index, action == 0);
    else if (app_loxone_audio_summary(home_config.cards[index].id, home_revision, &audio_ui_summary) &&
             audio_ui_summary.track.mode != LX_AUDIO_MODE_UNKNOWN)
        app_loxone_audio_command(home_config.cards[index].id, home_revision,
            audio_ui_summary.track.mode == LX_AUDIO_MODE_PLAY ? LX_AUDIO_PAUSE : LX_AUDIO_PLAY, 0, true);
}

static void audio_card_build(lv_obj_t *card, lv_obj_t *title, unsigned slot, unsigned index)
{
    audio_ui_card_t *ui = &audio_cards[slot];
    memset(ui, 0, sizeof(*ui));
    lv_obj_delete(home_icons[slot]);
    home_icons[slot] = NULL;
    const lv_image_dsc_t *skin = home_audio_skin();
    if (skin)
    {
        lv_obj_t *image = lv_image_create(card);
        lv_image_set_src(image, skin);
        lv_obj_set_pos(image, 0, 0);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_move_to_index(image, 0);
    }
    lv_obj_set_pos(title, 18, 10);
    lv_obj_set_size(title, 256, 24);
    lv_obj_set_style_text_color(title, c(home_night ? 0xEFF6F5 : 0x142B35), 0);
    ui->screen = shape(card, 30, 51, 98, 98, 0x20343A, 5);
    lv_obj_remove_flag(ui->screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(ui->screen, audio_card_event, LV_EVENT_CLICKED, (void *)(uintptr_t)(index * 3));
    ui->cover.object = lv_image_create(ui->screen);
    ui->cover.size = 112;
    ui->cover.display_size = 96;
    int icon = home_icon_index(LX_AUDIO_ICON_ID);
    ui->cover.fallback = icon >= 0 ? &home_images[icon] : NULL;
    if (ui->cover.fallback)
        lv_image_set_src(ui->cover.object, ui->cover.fallback);
    lv_obj_center(ui->cover.object);
    lv_obj_remove_flag(ui->cover.object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (!skin)
    {
        lv_obj_t *text_screen = shape(card, 147, 49, 113, 48, 0x20343A, 5);
        lv_obj_remove_flag(text_screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
            LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    ui->title = lbl(card, "", 151, 53, APP_FONT_16, 0xD8ECE9);
    lv_obj_set_size(ui->title, 105, 40);
    lv_label_set_long_mode(ui->title, LV_LABEL_LONG_MODE_DOTS);
    ui->play = audio_button(card, LV_SYMBOL_PLAY, 147, 109, 48, 48);
    ui->volume = audio_button(card, LV_SYMBOL_VOLUME_MAX, 210, 109, 48, 48);
    lv_obj_add_event_cb(ui->play, audio_card_event, LV_EVENT_CLICKED, (void *)(uintptr_t)(index * 3 + 2));
    lv_obj_add_event_cb(ui->volume, audio_card_event, LV_EVENT_CLICKED, (void *)(uintptr_t)(index * 3 + 1));
    ui->status = lbl(card, "", 18, 174, APP_FONT_14, skin ? 0xD8ECE9 : home_night ? 0xABC3C5 : 0x52686A);
    lv_obj_set_size(ui->status, 256, 18);
    lv_label_set_long_mode(ui->status, LV_LABEL_LONG_MODE_DOTS);
}

static void audio_card_update(unsigned slot, unsigned index, bool live)
{
    audio_ui_card_t *ui = &audio_cards[slot];
    bool current = app_loxone_audio_summary(home_config.cards[index].id, home_revision, &audio_ui_summary);
    if (!current)
        memset(&audio_ui_summary, 0, sizeof(audio_ui_summary));
    const lx_audio_summary_t *summary = &audio_ui_summary;
    audio_cached_text(ui->title, ui->title_cache, sizeof(ui->title_cache), audio_title_text(summary, false));
    audio_cached_text(ui->status, ui->status_cache, sizeof(ui->status_cache), audio_status_text(summary));
    audio_symbol(ui->play, summary->track.mode == LX_AUDIO_MODE_PLAY ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    light_ui_enable(ui->play, live && current && summary->controllable && !home_command_wait &&
        summary->track.mode != LX_AUDIO_MODE_UNKNOWN);
    light_ui_enable(ui->volume, live && current);
    light_ui_enable(ui->screen, live && current);
    if (current)
        audio_picture_update(&ui->cover, slot, summary);
}
