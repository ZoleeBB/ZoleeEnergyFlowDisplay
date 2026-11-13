/* A hét közös vezérlő LVGL-felülete. A GUI-ban nincs katalógusolvasás,
 * hálózat vagy SD-munka; a szerkesztett cél és a tényleges állapot külön él. */
#include "app_loxone_controls_service.h"

typedef struct
{
    lv_obj_t *buttons[3];
    unsigned count;
} controls_ui_card_t;

static TEMPLATE_RAM controls_ui_card_t controls_cards[LX_HOME_MAX_CARDS];
static lv_obj_t *controls_overlay, *controls_panel, *controls_slider;
static lv_obj_t *controls_actual, *controls_target, *controls_note, *controls_apply;
#define CONTROLS_TEXT_SIZE 336U
static TEMPLATE_RAM char controls_row_text[6][CONTROLS_TEXT_SIZE];
static TEMPLATE_RAM char controls_note_text[CONTROLS_TEXT_SIZE];
static lv_obj_t *controls_permanent, *controls_off, *controls_previous, *controls_next;
static lv_obj_t *controls_decrease, *controls_increase;
static lv_obj_t *controls_rows[6], *controls_page_label;
static TEMPLATE_RAM uint16_t controls_window_order[LX_CONTROLS_MAX_WINDOWS];
static unsigned controls_order_revision, controls_order_count;
static bool controls_order_ready;
static unsigned controls_index, controls_revision, controls_page, controls_generation;
static unsigned controls_list_start;
static double controls_target_value;
static bool controls_error, controls_submitted;
static char controls_id[LX_HOME_ID_SIZE];

static bool controls_ui_is_open(void)
{
    return controls_overlay != NULL;
}

/* A DOTS mód az LVGL saját szövegét lerövidíti. A teljes kijelzendő érték
 * külön cache-e szükséges, különben a hosszú sor minden frissítéskor
 * új invalidációt okozna. A cache a kis látható felugróablakhoz tartozik. */
static void controls_cached_text(lv_obj_t *label, char *cache, size_t capacity, const char *text)
{
    if (!label || !capacity)
        return;
    size_t length = strlen(text);
    if (length >= capacity)
    {
        length = capacity - 1;
        while (length && ((unsigned char)text[length] & 0xc0U) == 0x80U)
            --length;
    }
    if (strlen(cache) == length && !memcmp(cache, text, length))
        return;
    memcpy(cache, text, length);
    cache[length] = 0;
    lv_label_set_text(label, cache);
}

static bool controls_other_modal(void)
{
    return lighting_ui_is_open() || shading_ui_is_open() || audio_ui_is_open();
}

static void controls_ui_reset(void)
{
    controls_generation = (controls_generation + 1) & 0x0fffffffU;
    if (!controls_generation)
        controls_generation = 1;
    if (controls_overlay)
        lv_obj_delete(controls_overlay);
    controls_overlay = controls_panel = controls_slider = NULL;
    controls_actual = controls_target = controls_note = controls_apply = NULL;
    controls_permanent = controls_off = controls_previous = controls_next = NULL;
    controls_decrease = controls_increase = NULL;
    controls_order_ready = false;
    controls_order_revision = controls_order_count = 0;
    controls_page_label = NULL;
    memset(controls_rows, 0, sizeof(controls_rows));
    memset(controls_row_text, 0, sizeof(controls_row_text));
    memset(controls_note_text, 0, sizeof(controls_note_text));
    controls_id[0] = 0;
    controls_error = controls_submitted = false;
    controls_list_start = 0;
}

static bool controls_current(lx_controls_summary_t *summary)
{
    if (!controls_overlay || controls_other_modal() || s_pair_pad ||
        loxone_display_is_held() || !home_connected || controls_revision != home_revision ||
        controls_page != home_card_page || home_request_get() != 2 + (int)controls_page ||
        controls_index >= home_config.card_count || controls_index < home_first_card ||
        controls_index >= home_first_card + home_card_count ||
        strcmp(controls_id, home_config.cards[controls_index].id) ||
        !lx_home_card_is_controls(home_config.cards[controls_index].type))
        return false;
    return app_loxone_controls_summary(controls_id, controls_revision, summary) && summary->available;
}

static void controls_value_text(char *out, size_t capacity, const lx_controls_summary_t *summary,
                                 double value)
{
    snprintf(out, capacity, "%s%.*f%s%s", summary->prefix, (int)summary->decimals, value,
             summary->unit[0] ? " " : "", summary->unit);
}

static const char *controls_motion_text(const lx_controls_summary_t *summary)
{
    return !summary->motion_valid ? "" : summary->motion > 0 ? tr("Nyílik", "Opening") :
           summary->motion < 0 ? tr("Záródik", "Closing") : tr("Áll", "Stopped");
}

static void controls_timer_text(char *out, size_t size, const lx_controls_summary_t *summary)
{
    if (!summary->timer_valid)
        snprintf(out, size, "%s", tr("Nincs időzítőadat", "No timer data"));
    else if (summary->timer_seconds < 0)
        snprintf(out, size, "%s", tr("Tartósan bekapcsolva", "Permanently on"));
    else if (summary->timer_seconds > 0)
    {
        unsigned seconds = (unsigned)ceil(summary->timer_seconds);
        snprintf(out, size, tr("Hátralévő: %u:%02u", "Remaining: %u:%02u"), seconds / 60, seconds % 60);
    }
    else
        snprintf(out, size, "%s", summary->timer_running ?
            tr("Visszajelzésre vár", "Waiting for status") : tr("Kikapcsolva", "Off"));
}

static void controls_monitor_state(char *out, size_t size, const lx_controls_window_t *row)
{
    if (!row->valid)
        snprintf(out, size, "%s", tr("Nincs adat", "No data"));
    else if (!row->state)
        snprintf(out, size, "%s", tr("Nem elérhető", "Offline"));
    else
    {
        const char *position = row->state & 4 ? tr("Nyitva", "Open") :
            row->state & 2 ? tr("Bukóra nyitva", "Tilted") :
            row->state & 1 ? tr("Zárva", "Closed") : "";
        const char *lock = row->state & 16 ? tr("Nincs reteszelve", "Unlocked") :
            row->state & 8 ? tr("Reteszelve", "Locked") : "";
        snprintf(out, size, "%s%s%s", position, position[0] && lock[0] ? " | " : "", lock);
    }
}

static void controls_count_text(char *out, size_t capacity, const lx_controls_summary_t *summary,
                                 unsigned count, const char *caption)
{
    if (summary->counts_known & (1U << count))
        snprintf(out, capacity, "%s: %u", caption, summary->counts[count]);
    else
        snprintf(out, capacity, "%s: —", caption);
}

static void controls_ui_update(void)
{
    if (!controls_overlay)
        return;
    lx_controls_summary_t summary;
    if (!controls_current(&summary))
    {
        controls_ui_reset();
        return;
    }
    char text[CONTROLS_TEXT_SIZE], value[100];
    bool allowed = summary.controllable && !home_command_wait;
    if (summary.type == LX_HOME_CARD_WINDOW_MONITOR)
    {
        /* Az indexek csak állapotesemény után rendeződnek újra. A GUI
         * üresjáratban továbbra is csak a hat látható sort kéri le. */
        if (!controls_order_ready || controls_order_revision != summary.list_revision ||
            controls_order_count != summary.window_count)
        {
            if (!app_loxone_controls_window_order(controls_id, controls_revision, summary.list_revision,
                                                   controls_window_order, LX_CONTROLS_MAX_WINDOWS))
                return;
            controls_order_ready = true;
            controls_order_revision = summary.list_revision;
            controls_order_count = summary.window_count;
            if (controls_list_start >= controls_order_count)
                controls_list_start = controls_order_count ? ((controls_order_count - 1) / 6) * 6 : 0;
        }
        char counts[6][44];
        const char *captions[] = {tr("Nyitva", "Open"), tr("Zárva", "Closed"),
            tr("Bukó", "Tilted"), tr("Offline", "Offline"), tr("Reteszelve", "Locked"),
            tr("Nem reteszelt", "Unlocked")};
        for (unsigned i = 0; i < 6; ++i)
            controls_count_text(counts[i], sizeof(counts[i]), &summary, i, captions[i]);
        snprintf(text, sizeof(text), "%s  |  %s  |  %s\n%s  |  %s  |  %s",
                 counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
        label_text_if_changed(controls_actual, text);
        for (unsigned i = 0; i < 6; ++i)
        {
            lx_controls_window_t row;
            bool exists = controls_list_start + i < summary.window_count &&
                app_loxone_controls_window(controls_id, controls_revision,
                                             controls_window_order[controls_list_start + i], &row);
            light_ui_hidden(controls_rows[i], !exists);
            if (!exists)
                continue;
            controls_monitor_state(value, sizeof(value), &row);
            snprintf(text, sizeof(text), "%s%s%s%s%s\n%s", row.name[0] ? row.name : tr("Nyílászáró", "Window / door"),
                row.room[0] ? " · " : "", row.room, row.place[0] ? " · " : "", row.place, value);
            controls_cached_text(controls_rows[i], controls_row_text[i], sizeof(controls_row_text[i]), text);
        }
        light_ui_enable(controls_previous, controls_list_start > 0);
        light_ui_enable(controls_next, controls_list_start + 6 < summary.window_count);
        snprintf(text, sizeof(text), "%u–%u / %u", summary.window_count ? controls_list_start + 1 : 0,
            controls_list_start + 6 < summary.window_count ? controls_list_start + 6 : summary.window_count,
            summary.window_count);
        label_text_if_changed(controls_page_label, text);
        controls_cached_text(controls_note, controls_note_text, sizeof(controls_note_text), summary.list_truncated ?
            tr("A részletlista elérte a kijelző korlátját. Az összesítő számok teljesek.",
               "The detail list reached the display limit. Summary counts remain complete.") :
            !summary.list_valid ? tr("A részletállapotok még nem érkeztek meg.", "Waiting for item states.") :
            tr("Az összesítő számokat a Loxone számítja.", "Summary counts are provided by Loxone."));
        return;
    }
    if (summary.type == LX_HOME_CARD_TIMED_SWITCH)
    {
        controls_timer_text(text, sizeof(text), &summary);
        label_text_if_changed(controls_actual, text);
        if (summary.timer_total_valid)
            snprintf(text, sizeof(text), tr("Loxone időzítés: %.0f másodperc", "Loxone timer: %.0f seconds"),
                     summary.timer_total);
        else
            snprintf(text, sizeof(text), "%s", tr("Loxone időzítés: nincs adat", "Loxone timer: no data"));
        label_text_if_changed(controls_target, text);
        light_ui_enable(controls_permanent, allowed && home_config.cards[controls_index].allow_permanent);
        light_ui_enable(controls_off, allowed);
    }
    else
    {
        if (summary.value_valid)
            controls_value_text(value, sizeof(value), &summary, summary.value);
        else
            snprintf(value, sizeof(value), "%s", tr("Nincs adat", "No data"));
        snprintf(text, sizeof(text), tr("Aktuális: %s", "Current: %s"), value);
        label_text_if_changed(controls_actual, text);
        controls_value_text(value, sizeof(value), &summary, controls_target_value);
        snprintf(text, sizeof(text), tr("Célérték: %s", "Target: %s"), value);
        label_text_if_changed(controls_target, text);
        light_ui_enable(controls_slider, allowed && summary.range_valid);
        light_ui_enable(controls_decrease, allowed && summary.range_valid && controls_target_value > summary.minimum);
        light_ui_enable(controls_increase, allowed && summary.range_valid && controls_target_value < summary.maximum);
        bool direction_allowed = summary.type != LX_HOME_CARD_WINDOW ||
            ((summary.open_allowed && summary.close_allowed) ||
             (summary.value_valid &&
              (controls_target_value <= summary.value || summary.open_allowed) &&
              (controls_target_value >= summary.value || summary.close_allowed)));
        light_ui_enable(controls_apply, allowed && summary.range_valid && direction_allowed);
        light_ui_enable(controls_off, allowed);
    }
    const char *note = !summary.controllable ? tr("A vezérlés most nem engedélyezett.", "Control is currently unavailable.") :
        home_command_wait ? tr("Kérés küldése...", "Sending request...") :
        controls_error || summary.failed ? tr("A kérés nem igazolható; ellenőrizd a visszajelzést.", "Request unconfirmed; check feedback.") :
        controls_submitted && summary.type != LX_HOME_CARD_DIMMER ?
            (home_status[0] ? home_feedback() : tr("A kérés eredménye még nem ismert.", "The request result is not yet known.")) :
        controls_submitted ? tr("A cél elküldve; az aktuális értéket a Loxone jelzi vissza.", "Target sent; actual values come from Loxone.") :
        summary.type == LX_HOME_CARD_DIMMER ? tr("A csúszka húzás közben állít.", "The slider sends changes while dragging.") :
        summary.type == LX_HOME_CARD_TIMED_SWITCH ? tr("A Ki gomb a tartós bekapcsolást is megszünteti.", "Off also ends permanent mode.") :
        tr("A célértéket a Beállítás gomb küldi el.", "Apply sends the target value.");
    if (summary.reason[0])
        snprintf(text, sizeof(text), "%s · %s", summary.reason, note);
    else
        snprintf(text, sizeof(text), "%s", note);
    controls_cached_text(controls_note, controls_note_text, sizeof(controls_note_text), text);
}

static bool controls_modal_event(lv_event_t *event, lx_controls_summary_t *summary)
{
    lv_event_stop_bubbling(event);
    if (((unsigned)(uintptr_t)lv_event_get_user_data(event) >> 4) != controls_generation)
        return false;
    if (!controls_current(summary))
    {
        controls_ui_reset();
        return false;
    }
    return true;
}

static void controls_close_event(lv_event_t *event)
{
    if (lv_event_get_target(event) != lv_event_get_current_target(event))
        return;
    lv_event_stop_bubbling(event);
    if (((unsigned)(uintptr_t)lv_event_get_user_data(event) >> 4) != controls_generation)
        return;
    lv_indev_t *input = lv_indev_active();
    if (input)
        lv_indev_wait_release(input);
    controls_ui_reset();
}

static void controls_slider_event(lv_event_t *event)
{
    lx_controls_summary_t summary;
    if (!controls_modal_event(event, &summary) || !summary.controllable || !summary.range_valid ||
        (home_command_wait && summary.type != LX_HOME_CARD_DIMMER))
        return;
    double fraction = (double)lv_slider_get_value(controls_slider) / 10000;
    controls_target_value = summary.minimum +
        round(fraction * (summary.maximum - summary.minimum) / summary.step) * summary.step;
    if (controls_target_value > summary.maximum)
        controls_target_value = summary.maximum;
    controls_error = controls_submitted = false;
    if (summary.type == LX_HOME_CARD_DIMMER)
    {
        controls_error = app_loxone_controls_set(controls_id, controls_revision, controls_target_value) != ESP_OK;
        controls_submitted = !controls_error;
    }
    controls_ui_update();
}

static void controls_slider_sync(const lx_controls_summary_t *summary)
{
    double span = summary->maximum - summary->minimum;
    double fraction = summary->range_valid && span > 0 ?
        (controls_target_value - summary->minimum) / span : 0;
    int position = fraction <= 0 ? 0 : fraction >= 1 ? 10000 : (int)lround(fraction * 10000);
    lv_slider_set_value(controls_slider, position, LV_ANIM_OFF);
}

static void controls_step_event(lv_event_t *event)
{
    lx_controls_summary_t summary;
    if (!controls_modal_event(event, &summary) || !summary.controllable || !summary.range_valid ||
        (summary.type != LX_HOME_CARD_DIMMER && summary.type != LX_HOME_CARD_SLIDER) ||
        (home_command_wait && summary.type != LX_HOME_CARD_DIMMER))
        return;
    bool increase = ((unsigned)(uintptr_t)lv_event_get_user_data(event) & 1U) != 0;
    if ((increase && controls_target_value >= summary.maximum) ||
        (!increase && controls_target_value <= summary.minimum))
        return;
    /* Az aktuális visszajelzés lehet két lépés közötti érték, vagy a
     * dimmer kikapcsolt nullája a minimum alatt. A szomszédos érvényes
     * lépésre állunk; az alsó/felső végpontot nem lépjük túl. */
    double grid = (controls_target_value - summary.minimum) / summary.step;
    double step = increase ? floor(grid + 1e-9) + 1 : ceil(grid - 1e-9) - 1;
    double target = summary.minimum + step * summary.step;
    if (!isfinite(target))
        return;
    if (target < summary.minimum)
        target = summary.minimum;
    if (target > summary.maximum)
        target = summary.maximum;
    if (target == controls_target_value)
        return;
    controls_target_value = target;
    controls_slider_sync(&summary);
    controls_error = controls_submitted = false;
    if (summary.type == LX_HOME_CARD_DIMMER)
    {
        controls_error = app_loxone_controls_set(controls_id, controls_revision, target) != ESP_OK;
        controls_submitted = !controls_error;
    }
    controls_ui_update();
}

static void controls_modal_command(lv_event_t *event)
{
    lx_controls_summary_t summary;
    if (!controls_modal_event(event, &summary) || !summary.controllable || home_command_wait)
        return;
    unsigned operation = (unsigned)(uintptr_t)lv_event_get_user_data(event) & 15U;
    esp_err_t error = operation == LX_CONTROLS_SET ?
        app_loxone_controls_set(controls_id, controls_revision, controls_target_value) :
        app_loxone_controls_command(controls_id, controls_revision, (lx_controls_action_t)operation);
    controls_error = error != ESP_OK;
    controls_submitted = !controls_error;
    if (!controls_error)
        home_command_wait = true;
    controls_ui_update();
}

static void controls_list_page_event(lv_event_t *event)
{
    lx_controls_summary_t summary;
    if (!controls_modal_event(event, &summary))
        return;
    unsigned next = (unsigned)(uintptr_t)lv_event_get_user_data(event) & 15U;
    if (next && controls_list_start + 6 < summary.window_count)
        controls_list_start += 6;
    if (!next && controls_list_start >= 6)
        controls_list_start -= 6;
    controls_ui_update();
}

static lv_obj_t *controls_modal_button(const char *text, int x, int y, int width,
                                        lx_controls_action_t action)
{
    lv_obj_t *button = pairing_button(controls_panel, text, x, y, width, 54);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(button, controls_modal_command, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)((controls_generation << 4) | (unsigned)action));
    return button;
}

static void controls_ui_open(unsigned index)
{
    if (home_modal_is_open() || s_pair_pad || loxone_display_is_held() || !home_connected ||
        home_command_wait || home_page != 2 || home_request_get() != 2 + (int)home_card_page ||
        index >= home_config.card_count || index < home_first_card || index >= home_first_card + home_card_count)
        return;
    lx_controls_summary_t summary;
    if (!app_loxone_controls_summary(home_config.cards[index].id, home_revision, &summary) || !summary.available ||
        (summary.type != LX_HOME_CARD_DIMMER && summary.type != LX_HOME_CARD_SLIDER &&
         summary.type != LX_HOME_CARD_TIMED_SWITCH && summary.type != LX_HOME_CARD_WINDOW_MONITOR &&
         summary.type != LX_HOME_CARD_WINDOW))
        return;
    controls_ui_reset();
    controls_index = index;
    controls_revision = home_revision;
    controls_page = home_card_page;
    controls_target_value = summary.value_valid ? summary.value : summary.minimum;
    snprintf(controls_id, sizeof(controls_id), "%s", home_config.cards[index].id);
    void *generation = (void *)(uintptr_t)(controls_generation << 4);
    bool monitor = summary.type == LX_HOME_CARD_WINDOW_MONITOR;
    int width = monitor ? 940 : 780, height = monitor ? 660 : 460;
    controls_overlay = shape(s_screen, 0, 0, 1280, 800, 0x000000, 0);
    bg_opa_if_changed(controls_overlay, 150, 0);
    lv_obj_remove_flag(controls_overlay, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(controls_overlay, controls_close_event, LV_EVENT_CLICKED, generation);
    controls_panel = shape(controls_overlay, (1280 - width) / 2, (800 - height) / 2,
                           width, height, home_night ? 0x14272D : 0xFBFDF9, 24);
    lv_obj_remove_flag(controls_panel, LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lbl(controls_panel, home_config.cards[index].name, 28, 22, APP_FONT_28, light_ui_foreground());
    lv_obj_set_width(title, width - 130);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *close = pairing_button(controls_panel, "X", width - 76, 12, 56, 56);
    lv_obj_remove_flag(close, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(close, controls_close_event, LV_EVENT_CLICKED, generation);
    controls_actual = lbl(controls_panel, "", 28, 88, monitor ? APP_FONT_20 : APP_FONT_28, light_ui_foreground());
    lv_obj_set_width(controls_actual, width - 56);
    if (monitor)
    {
        for (unsigned i = 0; i < 6; ++i)
        {
            controls_rows[i] = lbl(controls_panel, "", 30, 164 + (int)i * 64, APP_FONT_20, light_ui_foreground());
            lv_obj_set_size(controls_rows[i], width - 60, 58);
            lv_label_set_long_mode(controls_rows[i], LV_LABEL_LONG_MODE_DOTS);
        }
        controls_previous = pairing_button(controls_panel, tr("Előző", "Previous"), 28, 554, 180, 52);
        controls_next = pairing_button(controls_panel, tr("Következő", "Next"), width - 208, 554, 180, 52);
        lv_obj_remove_flag(controls_previous, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_remove_flag(controls_next, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(controls_previous, controls_list_page_event, LV_EVENT_CLICKED, generation);
        lv_obj_add_event_cb(controls_next, controls_list_page_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)((controls_generation << 4) | 1U));
        controls_page_label = lbl(controls_panel, "", 230, 568, APP_FONT_20, light_ui_muted());
        lv_obj_set_width(controls_page_label, width - 460);
        lv_obj_set_style_text_align(controls_page_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    else
    {
        controls_target = lbl(controls_panel, "", 28, 140, APP_FONT_28, home_night ? 0x68E0D5 : 0x006B69);
        lv_obj_set_width(controls_target, width - 56);
        if (summary.type == LX_HOME_CARD_TIMED_SWITCH)
        {
            controls_permanent = controls_modal_button(tr("Tartós be", "Permanent on"), 28, 270, 350, LX_CONTROLS_ON);
            bool permanent = home_config.cards[index].allow_permanent;
            light_ui_hidden(controls_permanent, !permanent);
            controls_off = controls_modal_button(tr("Ki", "Off"), permanent ? 402 : 28, 270,
                                                 permanent ? 350 : 724, LX_CONTROLS_OFF);
        }
        else
        {
            bool stepped = summary.type == LX_HOME_CARD_DIMMER || summary.type == LX_HOME_CARD_SLIDER;
            controls_slider = lv_slider_create(controls_panel);
            lv_obj_set_pos(controls_slider, stepped ? 120 : 54, 230);
            lv_obj_set_size(controls_slider, width - (stepped ? 240 : 108), 28);
            lv_slider_set_range(controls_slider, 0, 10000);
            controls_slider_sync(&summary);
            lv_obj_remove_flag(controls_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
            lv_obj_add_event_cb(controls_slider, controls_slider_event, LV_EVENT_VALUE_CHANGED, generation);
            lv_obj_add_event_cb(controls_slider, controls_slider_event, LV_EVENT_RELEASED, generation);
            if (stepped)
            {
                controls_decrease = pairing_button(controls_panel, "-", 28, 216, 64, 56);
                controls_increase = pairing_button(controls_panel, "+", width - 92, 216, 64, 56);
                lv_obj_remove_flag(controls_decrease, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_remove_flag(controls_increase, LV_OBJ_FLAG_GESTURE_BUBBLE);
                lv_obj_add_event_cb(controls_decrease, controls_step_event, LV_EVENT_CLICKED, generation);
                lv_obj_add_event_cb(controls_increase, controls_step_event, LV_EVENT_CLICKED,
                                    (void *)(uintptr_t)((controls_generation << 4) | 1U));
            }
            if (summary.type != LX_HOME_CARD_DIMMER)
                controls_apply = controls_modal_button(tr("Beállítás", "Apply"), stepped ? 28 : 264, 332,
                                                        width - (stepped ? 56 : 292), LX_CONTROLS_SET);
            if (summary.type == LX_HOME_CARD_WINDOW)
            {
                lv_obj_t *minimum = lbl(controls_panel, tr("0% · Zárva", "0% · Closed"),
                                        54, 276, APP_FONT_20, light_ui_muted());
                lv_obj_set_width(minimum, 300);
                lv_obj_t *maximum = lbl(controls_panel, tr("100% · Nyitva", "100% · Open"),
                                        width - 354, 276, APP_FONT_20, light_ui_muted());
                lv_obj_set_width(maximum, 300);
                lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_RIGHT, 0);
                controls_off = controls_modal_button("Stop", 28, 332, 210, LX_CONTROLS_STOP);
            }
        }
    }
    controls_note = lbl(controls_panel, "", 28, height - 42, APP_FONT_16, light_ui_muted());
    lv_obj_set_size(controls_note, width - 56, 32);
    lv_label_set_long_mode(controls_note, LV_LABEL_LONG_MODE_DOTS);
    controls_ui_update();
}

static void controls_card_event(lv_event_t *event)
{
    lv_event_stop_bubbling(event);
    unsigned encoded = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned index = encoded / 16, operation = encoded % 16;
    if (home_modal_is_open() || s_pair_pad || loxone_display_is_held() || !home_connected || home_command_wait ||
        home_page != 2 || home_request_get() != 2 + (int)home_card_page || index >= home_config.card_count ||
        index < home_first_card || index >= home_first_card + home_card_count)
        return;
    if (operation == 15)
    {
        controls_ui_open(index);
        return;
    }
    lx_controls_summary_t summary;
    if (!app_loxone_controls_summary(home_config.cards[index].id, home_revision, &summary) ||
        !summary.available || !summary.controllable)
        return;
    lx_controls_action_t action = (lx_controls_action_t)operation;
    if (action == LX_CONTROLS_PULSE && summary.type == LX_HOME_CARD_TIMED_SWITCH &&
        !summary.stairway && (summary.timer_running || summary.timer_seconds < 0))
        action = LX_CONTROLS_OFF;
    if (app_loxone_controls_command(home_config.cards[index].id, home_revision, action) == ESP_OK)
    {
        home_command_wait = true;
        snprintf(home_status, sizeof(home_status), "%s", tr("Kérés küldése...", "Sending request..."));
    }
}

static void controls_card_build(lv_obj_t *card, lv_obj_t *title, unsigned slot, unsigned index)
{
    controls_ui_card_t *ui = &controls_cards[slot];
    memset(ui, 0, sizeof(*ui));
    lx_home_card_type_t type = home_config.cards[index].type;
    lv_obj_set_pos(home_icons[slot], 98, 3);
    lv_obj_set_y(title, 98);
    lv_obj_set_height(title, lv_font_get_line_height(APP_FONT_20));
    home_labels[slot] = lbl(card, "", 10, 124, APP_FONT_16, light_ui_muted());
    home_text(home_labels[slot], 272);
    lv_obj_set_height(home_labels[slot], lv_font_get_line_height(APP_FONT_16));
    lv_label_set_long_mode(home_labels[slot], LV_LABEL_LONG_MODE_CLIP);
    bool popup = type == LX_HOME_CARD_DIMMER || type == LX_HOME_CARD_SLIDER ||
        type == LX_HOME_CARD_TIMED_SWITCH || type == LX_HOME_CARD_WINDOW_MONITOR || type == LX_HOME_CARD_WINDOW;
    if (popup)
    {
        /* Régi, érvényes SD-csomagban hiányozhat az új kép. A részletes
         * vezérlés ilyenkor is látható, képfájl nélküli érintési felületet kap. */
        if (!lv_image_get_src(home_icons[slot]))
        {
            lv_obj_delete(home_icons[slot]);
            home_icons[slot] = pairing_button(card, tr("Részletek", "Details"), 98, 12, 96, 80);
            lv_obj_set_style_text_font(lv_obj_get_child(home_icons[slot], 0), APP_FONT_14, 0);
            lv_obj_set_style_radius(home_icons[slot], 18, 0);
        }
        lv_obj_add_flag(home_icons[slot], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(home_icons[slot], controls_card_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index * 16 + 15));
    }
    const char *labels[3] = {tr("Be", "On"), tr("Ki", "Off"), ""};
    unsigned operations[3] = {LX_CONTROLS_ON, LX_CONTROLS_OFF, 0};
    ui->count = 2;
    if (type == LX_HOME_CARD_GATE || type == LX_HOME_CARD_WINDOW)
    {
        ui->count = 3;
        labels[0] = tr("Nyit", "Open");
        labels[1] = "Stop";
        labels[2] = tr("Zár", "Close");
        operations[0] = LX_CONTROLS_OPEN;
        operations[1] = LX_CONTROLS_STOP;
        operations[2] = LX_CONTROLS_CLOSE;
    }
    else if (type == LX_HOME_CARD_TIMED_SWITCH)
    {
        labels[0] = tr("Időzítés", "Timer");
        operations[0] = LX_CONTROLS_PULSE;
    }
    else if (type == LX_HOME_CARD_SLIDER || type == LX_HOME_CARD_WINDOW_MONITOR)
    {
        ui->count = 1;
        labels[0] = type == LX_HOME_CARD_SLIDER ? tr("Értékállítás", "Set value") : tr("Részletek", "Details");
        operations[0] = 15;
    }
    for (unsigned i = 0; i < ui->count; ++i)
    {
        int width = ui->count == 3 ? 76 : ui->count == 2 ? 122 : 260;
        int x = ui->count == 3 ? 16 + (int)i * 92 : 16 + (int)i * 138;
        ui->buttons[i] = pairing_button(card, labels[i], x, 146, width, 44);
        bg_color_if_changed(ui->buttons[i], c(0x007D7D), 0);
        lv_obj_set_style_radius(ui->buttons[i], 20, 0);
        lv_obj_add_flag(ui->buttons[i], LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_state(ui->buttons[i], LV_STATE_DISABLED);
        lv_obj_add_event_cb(ui->buttons[i], controls_card_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index * 16 + operations[i]));
    }
}

static void controls_card_update(unsigned slot, unsigned index, bool live)
{
    controls_ui_card_t *ui = &controls_cards[slot];
    lx_controls_summary_t summary;
    bool current = app_loxone_controls_summary(home_config.cards[index].id, home_revision, &summary);
    char text[150], value[100];
    if (!current || !summary.available)
        snprintf(text, sizeof(text), "%s", tr("Nincs adat", "No data"));
    else if (summary.type == LX_HOME_CARD_CENTRAL_LIGHTING)
        snprintf(text, sizeof(text), "%s", tr("Csoportos világításvezérlés", "Central lighting control"));
    else if (summary.type == LX_HOME_CARD_TIMED_SWITCH)
        controls_timer_text(text, sizeof(text), &summary);
    else if (summary.type == LX_HOME_CARD_WINDOW_MONITOR)
    {
        controls_count_text(text, sizeof(text), &summary, 0, tr("Nyitva", "Open"));
        controls_count_text(value, sizeof(value), &summary, 3, tr("Offline", "Offline"));
        size_t length = strlen(text);
        snprintf(text + length, sizeof(text) - length, " | %s", value);
    }
    else if (!summary.value_valid)
        snprintf(text, sizeof(text), "%s", tr("Érték: nincs adat", "Value unavailable"));
    else
    {
        if (summary.type == LX_HOME_CARD_GATE || summary.type == LX_HOME_CARD_WINDOW)
            snprintf(value, sizeof(value), tr("%.0f%% nyitva", "%.0f%% open"), summary.value);
        else
            controls_value_text(value, sizeof(value), &summary, summary.value);
        const char *motion = controls_motion_text(&summary);
        snprintf(text, sizeof(text), "%s%s%s", value, motion[0] ? " | " : "", motion);
    }
    label_text_if_changed(home_labels[slot], text);
    bool enabled = live && current && summary.available && summary.controllable && !home_command_wait;
    for (unsigned i = 0; i < ui->count; ++i)
    {
        bool allow = enabled;
        if (summary.type == LX_HOME_CARD_WINDOW_MONITOR)
            allow = live && current && summary.available && !home_command_wait;
        else if (summary.type == LX_HOME_CARD_GATE || summary.type == LX_HOME_CARD_WINDOW)
            allow = enabled && (i == 0 ? summary.open_allowed : i == 2 ? summary.close_allowed : true);
        light_ui_enable(ui->buttons[i], allow);
    }
    if (summary.type == LX_HOME_CARD_TIMED_SWITCH && ui->buttons[0])
    {
        const char *caption = summary.stairway && summary.timer_running ? tr("Újraindít", "Restart") :
            !summary.stairway && (summary.timer_running || summary.timer_seconds < 0) ? tr("Ki", "Off") :
            tr("Időzítés", "Timer");
        label_text_if_changed(lv_obj_get_child(ui->buttons[0], 0), caption);
    }
}
