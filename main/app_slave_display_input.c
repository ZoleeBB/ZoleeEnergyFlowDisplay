/* A teljes nyers érintéssorozatot még a widget-/gesztusfeldolgozás előtt szűrjük.
 * Nincs saját heapfoglalás, BSP-adatmutató felülírás vagy második érintésolvasás. */
#include "app_slave_display_input.h"
#include <string.h>

static app_slave_display_input_t *s_gates;

static void read_pointer(lv_indev_t *input, lv_indev_data_t *data)
{
    for (app_slave_display_input_t *gate = s_gates; gate; gate = gate->next)
    {
        if (gate->input != input)
            continue;
        gate->original_read(input, data);
        if (app_slave_display_pointer(gate->state, data->state == LV_INDEV_STATE_PRESSED,
                                      gate->clock_us()))
            data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
}

static void input_deleted(lv_event_t *event)
{
    app_slave_display_input_t *gate = lv_event_get_user_data(event);
    app_slave_display_input_unbind(gate);
}

bool app_slave_display_input_bind(app_slave_display_input_t *gate, lv_indev_t *input,
                                  app_slave_display_t *state, int64_t (*clock_us)(void))
{
    if (!gate || !input || !state || !clock_us ||
        lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER || !lv_indev_get_read_cb(input))
        return false;
    for (app_slave_display_input_t *entry = s_gates; entry; entry = entry->next)
        if (entry == gate || entry->input == input)
            return false;
    *gate = (app_slave_display_input_t){
        .input = input, .original_read = lv_indev_get_read_cb(input),
        .state = state, .clock_us = clock_us, .next = s_gates,
    };
    uint32_t events = lv_indev_get_event_count(input);
    lv_indev_add_event_cb(input, input_deleted, LV_EVENT_DELETE, gate);
    if (lv_indev_get_event_count(input) != events + 1)
    {
        memset(gate, 0, sizeof(*gate));
        return false;
    }
    s_gates = gate;
    lv_indev_set_read_cb(input, read_pointer);
    return true;
}

void app_slave_display_input_unbind(app_slave_display_input_t *gate)
{
    if (!gate)
        return;
    app_slave_display_input_t **entry = &s_gates;
    while (*entry && *entry != gate)
        entry = &(*entry)->next;
    if (!*entry)
        return;
    *entry = gate->next;
    if (lv_indev_get_read_cb(gate->input) == read_pointer)
        lv_indev_set_read_cb(gate->input, gate->original_read);
    lv_indev_remove_event_cb_with_user_data(gate->input, input_deleted, gate);
    memset(gate, 0, sizeof(*gate));
}
