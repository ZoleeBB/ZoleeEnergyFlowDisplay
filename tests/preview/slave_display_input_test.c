/* Valódi LVGL pointerfeldolgozás: a lenyelt felengedésből sem lehet kattintás. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "app_slave_display_input.h"
#include <assert.h>
#include <stdio.h>

static int64_t s_now;
static bool s_pressed;
static lv_point_t s_point;
static unsigned s_clicks, s_gestures, s_reads;

static int64_t clock_us(void)
{
    return s_now;
}

static void read_pointer(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    ++s_reads;
    data->point = s_point;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void clicked(lv_event_t *event)
{
    (void)event;
    ++s_clicks;
}

static void gesture(lv_event_t *event)
{
    (void)event;
    ++s_gestures;
}

static void pointer_sample(lv_indev_t *input, int x, int y, bool pressed)
{
    s_point = (lv_point_t){x, y};
    s_pressed = pressed;
    s_now += 40000;
    lv_tick_inc(40);
    unsigned before = s_reads;
    lv_indev_read(input);
    assert(s_reads == before + 1);
}

void app_test_slave_display_input(lv_display_t *display)
{
    lv_obj_t *root = lv_obj_create(lv_display_get_screen_active(display));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, lv_display_get_horizontal_resolution(display),
                    lv_display_get_vertical_resolution(display));
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_t *button = lv_button_create(root);
    lv_obj_set_pos(button, 50, 50);
    lv_obj_set_size(button, 200, 120);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_update_layout(root);

    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, read_pointer);
    int user_data, driver_data;
    lv_indev_set_user_data(input, &user_data);
    lv_indev_set_driver_data(input, &driver_data);
    app_slave_display_t state;
    app_slave_display_profile_t profile = {83, 17, true, 30};
    assert(app_slave_display_init(&state, &profile, s_now));
    app_slave_display_night(&state, true, s_now);
    app_slave_display_input_t gate = {0}, duplicate = {0};
    assert(app_slave_display_input_bind(&gate, input, &state, clock_us));
    assert(!app_slave_display_input_bind(&duplicate, input, &state, clock_us));
    assert(!app_slave_display_input_bind(&gate, input, &state, clock_us));
    assert(lv_indev_get_user_data(input) == &user_data);
    assert(lv_indev_get_driver_data(input) == &driver_data);
    assert(app_slave_display_brightness(&state, s_now) == 0);

    /* A gombon kezdődő ébresztő érintésből sem kattintás, sem gesztus nem jut át. */
    pointer_sample(input, 100, 100, true);
    assert(app_slave_display_brightness(&state, s_now) == 17);
    pointer_sample(input, 100, 400, true);
    pointer_sample(input, 100, 650, true);
    pointer_sample(input, 100, 650, false);
    assert(!s_clicks && !s_gestures && !state.swallow_pointer && !state.pointer_down);

    pointer_sample(input, 100, 100, true);
    pointer_sample(input, 100, 100, false);
    assert(s_clicks == 1);
    s_now += 31000000;
    assert(app_slave_display_brightness(&state, s_now) == 0);

    /* A PIN láthatósága érvényes kattintást enged, de nem tartja aktívnak a jelenlétet. */
    app_slave_display_keep_visible(&state, true, s_now);
    assert(app_slave_display_brightness(&state, s_now) == 17);
    pointer_sample(input, 100, 100, true);
    pointer_sample(input, 100, 100, false);
    assert(s_clicks == 2 && !state.presence_active);
    app_slave_display_keep_visible(&state, false, s_now);
    s_now += 31000000;
    assert(app_slave_display_brightness(&state, s_now) == 0);

    /* Egy már lenyelt érintést a közben megjelenő PIN sem darabolhat ketté. */
    pointer_sample(input, 100, 100, true);
    app_slave_display_keep_visible(&state, true, s_now);
    pointer_sample(input, 100, 100, false);
    assert(s_clicks == 2);
    pointer_sample(input, 100, 100, true);
    pointer_sample(input, 100, 100, false);
    assert(s_clicks == 3);

    app_slave_display_input_unbind(&gate);
    assert(!gate.input && lv_indev_get_read_cb(input) == read_pointer);
    assert(lv_indev_get_user_data(input) == &user_data);
    assert(lv_indev_get_driver_data(input) == &driver_data);
    assert(app_slave_display_input_bind(&gate, input, &state, clock_us));
    lv_indev_delete(input);
    assert(!gate.input);
    app_slave_display_input_unbind(&gate);
    lv_obj_delete(root);
    puts("PASS: actual LVGL wake-only touch/gesture/release gate, second click, PIN visibility, driver data and input deletion");
}
