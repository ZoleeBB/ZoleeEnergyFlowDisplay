/* A valódi 720×720 nézet és BLE kodek. Csak a rádió, óra és érintő hardver demó. */
#include "slave_4b_ui.h"
#include "app_ble_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
void app_test_slave_display_input(lv_display_t *display);
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
static HWND window;
#endif

static uint32_t pixels[720 * 720];
static lv_display_t *display;
static app_energy_sample_t sample;
static app_runtime_state_t runtime;
static app_ble_pairing_status_t pairing;
static lv_point_t pointer;
static bool pressed;
static unsigned submitted;
static uint32_t elapsed_ms;

bool app_ble_pairing_submit(uint32_t token, const char *pin)
{
    if (token != pairing.token || !pairing.pin_required)
        return false;
    if (!pin || !strcmp(pin, "123456"))
    {
        pairing.pin_required = false;
        submitted++;
        return true;
    }
    return false;
}

static void flush(lv_display_t *target, const lv_area_t *area, uint8_t *data)
{
    (void)area;
    (void)data;
#ifdef _WIN32
    if (window && lv_display_flush_is_last(target))
        InvalidateRect(window, NULL, FALSE);
#endif
    lv_display_flush_ready(target);
}

static void read_pointer(lv_indev_t *input, lv_indev_data_t *data)
{
    (void)input;
    data->point = pointer;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void scene(bool night)
{
    sample = (app_energy_sample_t){
        .power_grid_kw = night ? 1.5 : -1,
        .power_bat_kw = night ? .6 : -2,
        .power_pv_kw = night ? 0 : 7,
        .power_full_kw = night ? 2.1 : 4,
        .soc_bat_pct = 74, .battery_temperature_c = 32,
        .temperature_c = 18.2, .wind_speed_kmh = 12,
        .temperature_valid = true, .wind_speed_valid = true,
        .sun_radiation_valid = true, .precipitation_valid = true,
        .has_sun_radiation = !night, .night_mode = night, .status = 1,
        .received_epoch_s = 1792327020
    };
    runtime = (app_runtime_state_t){.slave_connected = true, .time_synced = true,
        .has_valid_sample = true, .ble_rssi = -51, .wifi_connected = true};
    snprintf(runtime.ble_peer, sizeof(runtime.ble_peer), "AA:BB:CC:DD:EE:FF");
}

static void step(unsigned ms)
{
    elapsed_ms += ms;
    lv_tick_inc(ms);
    slave_4b_ui_update(&sample, runtime.slave_connected && !runtime.slave_source_stale,
                    &runtime, sample.night_mode ? "21:37" : "14:37", ms / 1000.f);
    slave_4b_ui_pairing(&pairing);
    lv_timer_handler();
    lv_refr_now(display);
}

static unsigned invalidations;
static void count_invalidation(lv_event_t *event)
{
    (void)event;
    invalidations++;
}

static int save(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file)
        return 1;
    fprintf(file, "P6\n720 720\n255\n");
    for (unsigned i = 0; i < 720 * 720; i++)
    {
        uint8_t rgb[3] = {(uint8_t)(pixels[i] >> 16), (uint8_t)(pixels[i] >> 8), (uint8_t)pixels[i]};
        if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb))
        {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) != 0;
}

static uint64_t hash(void)
{
    uint64_t result = 1469598103934665603ULL;
    for (unsigned i = 0; i < 720 * 720; i++)
        result = (result ^ pixels[i]) * 1099511628211ULL;
    return result;
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)

static void click(int x, int y)
{
    pointer = (lv_point_t){x, y};
    pressed = true;
    step(40);
    pressed = false;
    step(40);
}

static int self_test(void)
{
    /* A master tényleges vezetékformátumán megy át a nappali demó. */
    app_config_t config = {0};
    config.mqtt.data_timeout_seconds = 45;
    config.display.day_brightness = 70;
    config.display.night_brightness = 25;
    uint8_t energy[APP_BLE_PACKET_SIZE], weather[APP_BLE_PACKET_SIZE];
    app_ble_protocol_encode(&sample, &config, 0, 1, energy, weather);
    app_energy_sample_t decoded;
    CHECK(app_ble_protocol_decode(energy, sizeof(energy), weather, sizeof(weather), 0, &decoded) == ESP_OK);
    CHECK(fabs(decoded.power_bat_kw + 2) < .001 && fabs(decoded.soc_bat_pct - 74) < .01);
    sample = decoded;
    step(33);
    CHECK(save("slave-4b-day.ppm") == 0);
    /* A tényleges LVGL körök helye és mérete mindkét váltási irányban állandó. */
    lv_obj_t *rings[4];
    lv_area_t ring_areas[4];
    unsigned ring_count = 0;
    lv_obj_t *screen = lv_screen_active();
    for (uint32_t i = 0; i < lv_obj_get_child_count(screen); i++)
    {
        lv_obj_t *child = lv_obj_get_child(screen, i);
        if (lv_obj_get_style_border_width(child, LV_PART_MAIN) == 3 &&
            lv_obj_get_width(child) == lv_obj_get_height(child))
        {
            CHECK(ring_count < 4);
            rings[ring_count] = child;
            lv_obj_get_coords(child, &ring_areas[ring_count]);
            int cx = lv_obj_get_x(child) + lv_obj_get_width(child) / 2;
            int cy = lv_obj_get_y(child) + lv_obj_get_height(child) / 2;
            CHECK(abs(cx - 360) + abs(cy - 360) == 230);
            CHECK(cx == 360 || cy == 360);
            CHECK(lv_obj_get_width(child) == 120);
            /* A kör előtti testvér a hozzá vezető energiaút. */
            CHECK(i > 0);
            lv_obj_t *path = lv_obj_get_child(screen, i - 1);
            int pw = lv_obj_get_width(path), ph = lv_obj_get_height(path);
            CHECK((pw == 170 && ph == 4) || (pw == 4 && ph == 170));
            ring_count++;
        }
    }
    CHECK(ring_count == 4);
    uint64_t day = hash();
    step(200);
    CHECK(hash() != day); /* A mozgó pontok valóban változtatják a renderelt képet. */
    scene(true);
    step(33);
    CHECK(save("slave-4b-night.ppm") == 0);
    CHECK(hash() != day);
    for (int mode = 0; mode < 3; mode++)
    {
        scene(mode != 1);
        step(33);
        for (unsigned i = 0; i < ring_count; i++)
        {
            lv_area_t area;
            lv_obj_get_coords(rings[i], &area);
            CHECK(area.x1 == ring_areas[i].x1 && area.y1 == ring_areas[i].y1 &&
                  area.x2 == ring_areas[i].x2 && area.y2 == ring_areas[i].y2);
        }
    }
    /* Opcionális időjárás: a hiányzó mező ne hagyjon ott régi ikont vagy értéket. */
    sample.temperature_valid = false;
    sample.wind_speed_valid = false;
    sample.sun_radiation_valid = false;
    sample.precipitation_valid = false;
    step(0);
    CHECK(save("slave-4b-no-weather.ppm") == 0);
    uint64_t no_weather = hash();
    sample.wind_speed_valid = true;
    step(0);
    CHECK(hash() != no_weather);
    scene(true);
    for (unsigned i = 0; i <= 100; i += 20)
    {
        sample.soc_bat_pct = i;
        step(40);
    }
    uint64_t full = hash();
    sample.soc_bat_pct = 0;
    step(0);
    CHECK(hash() != full);
    sample.status = 3;
    step(33);
    CHECK(save("slave-4b-island.ppm") == 0);
    runtime.slave_source_stale = true;
    step(33);
    uint64_t stale = hash();
    step(200);
    CHECK(hash() == stale); /* Lejárt adatnál nincs továbbmozgó energia. */
    CHECK(save("slave-4b-timeout.ppm") == 0);
    runtime.slave_connected = false;
    step(33);
    uint64_t disconnected = hash();
    step(200);
    CHECK(hash() == disconnected);
    CHECK(save("slave-4b-disconnected.ppm") == 0);
    /* Változatlan adatnál a 33 ms-os refresh nem érvényteleníthet: a feliratok,
     * a képek és a színek csak tényleges változáskor íródnak újra. */
    invalidations = 0;
    lv_display_add_event_cb(display, count_invalidation, LV_EVENT_INVALIDATE_AREA, NULL);
    for (unsigned i = 0; i < 30; i++)
        step(33);
    lv_display_remove_event_cb_with_user_data(display, count_invalidation, NULL);
    CHECK(invalidations == 0 && hash() == disconnected);
    scene(false);
    pairing = (app_ble_pairing_status_t){.pin_required = true, .token = 77, .seconds = 60};
    step(33);
    CHECK(save("slave-4b-pin.ppm") == 0);
    /* A valódi LVGL érintőútvonalon írjuk be a PIN-t. */
    click(205, 240);
    click(355, 240);
    click(505, 240);
    click(205, 330);
    click(355, 330);
    click(505, 330);
    click(505, 510);
    CHECK(submitted == 1 && !pairing.pin_required);
    /* Új kérésnél a régi számok/token nem élhetnek tovább. Törlés és elutasítás. */
    pairing = (app_ble_pairing_status_t){.pin_required = true, .token = 78, .seconds = 45};
    step(33);
    click(205, 240);
    click(205, 510);
    click(505, 510);
    CHECK(submitted == 1 && pairing.pin_required);
    click(350, 650);
    CHECK(submitted == 2 && !pairing.pin_required);
    click(650, 35);
    CHECK(save("slave-4b-diagnostics.ppm") == 0);
    puts("PASS: BLE decode, day/night, animation, optional weather, SOC 0..100, island, timeout/disconnect, touch PIN/reset/delete/cancel, diagnostics");
    app_test_slave_display_input(display);
    runtime.slave_connected = true;
    runtime.slave_source_stale = false;
    runtime.energy_flow_disabled = true;
    step(33);
    CHECK(save("slave-4b-energy-disabled.ppm") == 0);
    CHECK((pixels[0] & 0xffffff) == 0);
    uint64_t disabled_hash = hash();
    sample.power_pv_kw = 14;
    step(500);
    CHECK(hash() == disabled_hash);
    runtime.energy_flow_disabled = false;
    step(33);
    CHECK(hash() != disabled_hash);
    puts("PASS: actual 4B black logo state is stable without energy animation and returns to the energy screen");
    return 0;
}

#ifdef _WIN32
static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM key, LPARAM point)
{
    switch (message)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC context = BeginPaint(hwnd, &paint);
            BITMAPINFO bitmap = {0};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = 720;
            bitmap.bmiHeader.biHeight = -720;
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
            RECT rect;
            GetClientRect(hwnd, &rect);
            StretchDIBits(context, 0, 0, rect.right, rect.bottom, 0, 0, 720, 720,
                            pixels, &bitmap, DIB_RGB_COLORS, SRCCOPY);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        {
            RECT rect;
            GetClientRect(hwnd, &rect);
            pointer.x = rect.right ? (short)LOWORD(point) * 720 / rect.right : 0;
            pointer.y = rect.bottom ? (short)HIWORD(point) * 720 / rect.bottom : 0;
            pressed = (key & MK_LBUTTON) != 0;
            if (message == WM_LBUTTONDOWN)
                SetCapture(hwnd);
            if (message == WM_LBUTTONUP)
                ReleaseCapture();
            return 0;
        }
        case WM_KEYDOWN:
            if (key == 'N')
                scene(!sample.night_mode);
            if (key == 'T')
                runtime.slave_source_stale = !runtime.slave_source_stale;
            if (key == 'R')
                scene(sample.night_mode);
            if (key == VK_UP || key == VK_DOWN)
                sample.soc_bat_pct = fmax(0, fmin(100, sample.soc_bat_pct + (key == VK_UP ? 5 : -5)));
            if (key == 'P')
                pairing = (app_ble_pairing_status_t){.pin_required = true, .token = elapsed_ms, .seconds = 60};
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, message, key, point);
}
#endif

int main(int argc, char **argv)
{
    lv_init();
    display = lv_display_create(720, 720);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(input, read_pointer);
    slave_4b_ui_create(lv_screen_active());
    scene(false);
    if (argc > 1 && !strcmp(argv[1], "--self-test"))
    {
        int result = self_test();
        lv_deinit();
        return result;
    }
#ifdef _WIN32
    WNDCLASSA klass = {0};
    klass.lpfnWndProc = procedure;
    klass.hInstance = GetModuleHandle(NULL);
    klass.lpszClassName = "ZoleeSlave4B";
    klass.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&klass);
    RECT bounds = {0, 0, 720, 720};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowA(klass.lpszClassName, "Waveshare 4B | N: night | Up/Down: SOC | T: timeout | R: reconnect | P: PIN 123456",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top, NULL, NULL, klass.hInstance, NULL);
    if (!window)
        return 2;
    DWORD previous = GetTickCount();
    for (;;)
    {
        MSG event;
        while (PeekMessage(&event, NULL, 0, 0, PM_REMOVE))
        {
            if (event.message == WM_QUIT)
                return 0;
            TranslateMessage(&event);
            DispatchMessage(&event);
        }
        DWORD now = GetTickCount();
        if (now - previous >= 33)
        {
            step(now - previous);
            previous = now;
        }
        Sleep(5);
    }
#else
    fputs("Interactive mode requires Windows; use --self-test for headless rendering.\n", stderr);
    return 2;
#endif
}
