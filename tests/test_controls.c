/* A tényleges C-modell protokoll-, tartomány-, zár- és időkezelési tesztje. */
#include "app_loxone_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const ids[] = {
    "40000000-0000-0000-0000000000000001", "40000000-0000-0000-0000000000000002",
    "40000000-0000-0000-0000000000000003", "40000000-0000-0000-0000000000000004",
    "40000000-0000-0000-0000000000000005", "40000000-0000-0000-0000000000000006",
    "40000000-0000-0000-0000000000000007"
};

static void number(lx_controls_block_t *block, unsigned state, double value, int64_t now)
{
    lx_controls_block_event(block, block->states[state], true, value, NULL, 0, now);
}

static void text_event(lx_controls_block_t *block, unsigned state, const char *text)
{
    lx_controls_block_event(block, block->states[state], false, 0, text, strlen(text), 1000000);
}

static lx_controls_block_t *create(const cJSON *catalog, unsigned i)
{
    lx_controls_block_t *block = lx_controls_block_create(
        lx_controls_catalog_find(catalog, ids[i], (lx_home_card_type_t)(LX_HOME_CARD_DIMMER + i)));
    assert(block);
    return block;
}

static void unlock(lx_controls_block_t *block)
{
    text_event(block, LX_CONTROL_LOCK, "");
    number(block, LX_CONTROL_ERROR, 0, 1000000);
}

static void command(lx_controls_block_t *block, lx_controls_action_t action, double value,
                     const char *suffix)
{
    char out[96];
    assert(lx_controls_command(block, action, value, out));
    assert(strlen(out) > strlen(suffix));
    assert(!strcmp(out + strlen(out) - strlen(suffix), suffix));
    assert(strstr(out, "/40000000-0000-0000-000000000000006") != NULL);
}

static void test_dimmer(const cJSON *catalog)
{
    lx_controls_block_t *b = create(catalog, 0);
    char out[96];
    assert(!lx_controls_command(b, LX_CONTROLS_ON, 0, out));
    unlock(b);
    command(b, LX_CONTROLS_ON, 0, "/on");
    command(b, LX_CONTROLS_OFF, 0, "/off");
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 50, out));
    number(b, LX_CONTROL_MIN, 0, 1000000);
    number(b, LX_CONTROL_MAX, 100, 1000000);
    number(b, LX_CONTROL_STEP, 5, 1000000);
    number(b, LX_CONTROL_VALUE, 35, 1000000);
    lx_controls_summary_t summary;
    lx_controls_summary(b, 1000000, &summary);
    assert(summary.range_valid && summary.value_valid && summary.value == 35);
    command(b, LX_CONTROLS_SET, 23, "/25.000000");
    assert(!lx_controls_command(b, LX_CONTROLS_SET, NAN, out));
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 101, out));
    assert(lx_controls_queue(b, 10, 1000000));
    assert(lx_controls_queue(b, 40, 1000001));
    assert(lx_controls_take(b, 1000001, out) && strstr(out, "/40.000000"));
    assert(lx_controls_queue(b, 80, 1001000));
    assert(!lx_controls_take(b, 1050000, out));
    assert(lx_controls_take(b, 1100001, out) && strstr(out, "/80.000000"));
    lx_controls_summary(b, 1100001, &summary);
    assert(summary.value == 35); /* Elküldés nem gyárt visszajelzést. */
    assert(lx_controls_queue(b, 90, 1200000));
    assert(!lx_controls_take(b, 4000000, out));
    assert(b->failed && !b->dirty);
    text_event(b, LX_CONTROL_LOCK, "[\"locked\"]");
    assert(!lx_controls_command(b, LX_CONTROLS_ON, 0, out));
    unlock(b);
    number(b, LX_CONTROL_MIN, NAN, 1000000);
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 50, out));
    command(b, LX_CONTROLS_OFF, 0, "/off");
    lx_controls_block_free(b);
}

static void test_numeric_and_timer(const cJSON *catalog)
{
    lx_controls_block_t *b = create(catalog, 1);
    command(b, LX_CONTROLS_ON, 0, "/on");
    command(b, LX_CONTROLS_OFF, 0, "/reset");
    lx_controls_summary_t summary;
    lx_controls_summary(b, 1, &summary);
    assert(!summary.value_valid); /* A központi parancs nem összesített állapot. */
    lx_controls_block_free(b);
    b = create(catalog, 2);
    unlock(b);
    number(b, LX_CONTROL_VALUE, 21.5, 1000000);
    lx_controls_summary(b, 1000000, &summary);
    assert(summary.value_valid && summary.decimals == 1 && !strcmp(summary.unit, "°C"));
    assert(summary.minimum == -20 && summary.maximum == 60 && summary.step == .5);
    command(b, LX_CONTROLS_SET, 23.6, "/23.500000");
    number(b, LX_CONTROL_ERROR, 1, 1000000);
    char out[96];
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 23.5, out));
    number(b, LX_CONTROL_ERROR, NAN, 1000000);
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 23.5, out));
    lx_controls_block_free(b);
    b = create(catalog, 3);
    unlock(b);
    number(b, LX_CONTROL_DELAY, 10, 1000000);
    lx_controls_summary(b, 1000000, &summary);
    assert(!summary.timer_total_valid);
    number(b, LX_CONTROL_TOTAL, 30, 1000000);
    lx_controls_summary(b, 4250000, &summary);
    assert(summary.timer_running && summary.stairway && summary.timer_seconds == 6.75);
    assert(summary.timer_total_valid && summary.timer_total == 30);
    lx_controls_summary(b, 20000000, &summary);
    assert(summary.timer_running && summary.timer_seconds == 0); /* Nem hamis Ki. */
    command(b, LX_CONTROLS_PULSE, 0, "/pulse");
    command(b, LX_CONTROLS_ON, 0, "/on");
    number(b, LX_CONTROL_DELAY, -1, 20000000);
    lx_controls_summary(b, 30000000, &summary);
    assert(summary.timer_valid && !summary.timer_running && summary.timer_seconds == -1);
    command(b, LX_CONTROLS_OFF, 0, "/off");
    number(b, LX_CONTROL_DELAY, 0, 30000000);
    lx_controls_summary(b, 30000000, &summary);
    assert(!summary.timer_running && summary.timer_seconds == 0);
    lx_controls_block_free(b);
}

static void test_monitor(const cJSON *catalog)
{
    lx_controls_block_t *b = create(catalog, 4);
    assert(b->window_count == 4 && !strcmp(b->windows[2].name, "Window"));
    text_event(b, LX_CONTROL_WINDOWS, "9,2,20,0");
    assert(b->list_valid && b->windows[0].state == 9 && b->windows[2].state == 20 && b->windows[3].state == 0);
    for (unsigned i = 0; i < LX_CONTROLS_COUNT_COUNT; ++i)
        number(b, LX_CONTROL_NUM_OPEN + i, i + 1, 1000000);
    lx_controls_summary_t summary;
    lx_controls_summary(b, 1000000, &summary);
    assert(summary.counts_known == 63 && summary.counts[5] == 6 && !summary.controllable);
    const char *invalid[] = {"9,2,", "9,2,20", "9,2,32,0", "9,2,x,0", "9,2,20,0,1", "9,2,9999999999999,0"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        text_event(b, LX_CONTROL_WINDOWS, invalid[i]);
        assert(!b->list_valid);
        for (unsigned n = 0; n < b->window_count; ++n)
            assert(!b->windows[n].valid);
    }
    char out[96];
    assert(!lx_controls_command(b, LX_CONTROLS_OPEN, 0, out));
    lx_controls_block_free(b);
}

static void test_motion(const cJSON *catalog)
{
    lx_controls_block_t *b = create(catalog, 5);
    unlock(b);
    char out[96];
    assert(!lx_controls_command(b, LX_CONTROLS_OPEN, 0, out));
    number(b, LX_CONTROL_PREVENT_OPEN, 1, 1000000);
    number(b, LX_CONTROL_PREVENT_CLOSE, 0, 1000000);
    command(b, LX_CONTROLS_CLOSE, 0, "/close");
    command(b, LX_CONTROLS_STOP, 0, "/stop");
    assert(!lx_controls_command(b, LX_CONTROLS_OPEN, 0, out));
    number(b, LX_CONTROL_PREVENT_OPEN, 0, 1000000);
    command(b, LX_CONTROLS_OPEN, 0, "/open");
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 30, out));
    number(b, LX_CONTROL_VALUE, .45, 1000000);
    number(b, LX_CONTROL_MOTION, -1, 1000000);
    lx_controls_summary_t summary;
    lx_controls_summary(b, 1000000, &summary);
    assert(summary.value_valid && summary.value == 45 && summary.motion == -1 && summary.animation == 3);
    const char *icons[] = {"gate-garage", "gate-left", "gate-right", "gate-double", "gate-slide-left", "gate-slide-right"};
    for (unsigned i = 0; i < 6; ++i)
        assert(!strcmp(lx_controls_gate_icon((int)i), icons[i]));
    assert(!strcmp(lx_controls_gate_icon(6), "gate"));
    lx_controls_block_free(b);
    b = create(catalog, 6);
    unlock(b);
    text_event(b, LX_CONTROL_REASON, "Szélvédelem");
    command(b, LX_CONTROLS_OPEN, 0, "/fullopen");
    command(b, LX_CONTROLS_CLOSE, 0, "/fullclose");
    command(b, LX_CONTROLS_STOP, 0, "/stop");
    command(b, LX_CONTROLS_SET, 70, "/moveToPosition/70.000000");
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 100.1, out));
    number(b, LX_CONTROL_VALUE, 1.1, 1000000);
    lx_controls_summary(b, 1000000, &summary);
    assert(!summary.value_valid && !strcmp(summary.reason, "Szélvédelem"));
    strcpy(b->states[LX_CONTROL_PREVENT_OPEN], "40000000-0000-0000-0000000000001bf0");
    strcpy(b->states[LX_CONTROL_PREVENT_CLOSE], "40000000-0000-0000-0000000000001bf1");
    number(b, LX_CONTROL_PREVENT_OPEN, 1, 1000000);
    number(b, LX_CONTROL_PREVENT_CLOSE, 0, 1000000);
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 0, out)); /* Nincs ismert irány. */
    number(b, LX_CONTROL_VALUE, .45, 1000000);
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 70, out));
    command(b, LX_CONTROLS_SET, 20, "/moveToPosition/20.000000");
    command(b, LX_CONTROLS_SET, 45, "/moveToPosition/45.000000");
    number(b, LX_CONTROL_PREVENT_OPEN, 0, 1000000);
    number(b, LX_CONTROL_PREVENT_CLOSE, 1, 1000000);
    assert(!lx_controls_command(b, LX_CONTROLS_SET, 20, out));
    command(b, LX_CONTROLS_SET, 70, "/moveToPosition/70.000000");
    command(b, LX_CONTROLS_STOP, 0, "/stop");
    text_event(b, LX_CONTROL_LOCK, "{\"locked\":1}");
    assert(!lx_controls_command(b, LX_CONTROLS_STOP, 0, out));
    lx_controls_block_free(b);
}

static void test_config(void)
{
    cJSON *doc = cJSON_Parse("{\"schema\":2,\"pages\":[{\"id\":\"controls\",\"title\":\"Vezérlés\",\"cards\":[]}]}");
    cJSON *cards = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(doc, "pages"), 0), "cards");
    const char *types[] = {"dimmer", "central-lighting", "slider", "timed-switch", "window-monitor", "gate", "window", "audio"};
    for (unsigned i = 0; i < 8; ++i)
    {
        cJSON *card = cJSON_CreateObject();
        assert(card && cJSON_AddItemToArray(cards, card));
        assert(cJSON_AddStringToObject(card, "id", types[i]));
        assert(cJSON_AddStringToObject(card, "name", types[i]));
        assert(cJSON_AddStringToObject(card, "type", types[i]));
        assert(cJSON_AddStringToObject(card, "icon", "lamp"));
        assert(cJSON_AddStringToObject(card, "control_uuid", ids[i % 7]));
    }
    lx_home_config_t *home = calloc(1, sizeof(*home));
    char error[LX_HOME_ERROR_SIZE];
    assert(home && lx_home_config_parse(doc, home, error));
    assert(home->card_count == 8 && home->cards[3].allow_permanent);
    assert(home->cards[7].type == LX_HOME_CARD_AUDIO);
    assert(!strcmp(home->cards[1].icon, "central-lighting"));
    assert(!strcmp(home->cards[6].icon, "motor-window"));
    assert(!strcmp(home->cards[7].icon, "audio-player"));
    home->cards[3].allow_permanent = false;
    cJSON *saved = lx_home_config_json(home);
    assert(saved && lx_home_config_parse(saved, home, error));
    assert(!home->cards[3].allow_permanent);
    cJSON *timer = cJSON_GetArrayItem(cards, 3);
    assert(cJSON_AddStringToObject(timer, "allow_permanent", "false"));
    assert(!lx_home_config_parse(doc, home, error));
    assert(!home->cards[3].allow_permanent);
    cJSON_Delete(saved);
    cJSON_Delete(doc);
    free(home);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length > 0 && !fseek(file, 0, SEEK_SET));
    char *data = calloc((size_t)length + 1, 1);
    assert(data && fread(data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    cJSON *catalog = lx_catalog_parse(data, (size_t)length);
    assert(catalog);
    test_dimmer(catalog);
    test_numeric_and_timer(catalog);
    test_monitor(catalog);
    test_motion(catalog);
    test_config();
    cJSON *source = cJSON_Parse(data);
    free(data);
    cJSON *control = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(source, "controls"), ids[0]);
    assert(cJSON_AddNumberToObject(control, "restrictions", 2));
    data = cJSON_PrintUnformatted(source);
    cJSON *restricted = lx_catalog_parse(data, strlen(data));
    assert(restricted);
    lx_controls_block_t *blocked = create(restricted, 0);
    unlock(blocked);
    char out[96];
    assert(!lx_controls_command(blocked, LX_CONTROLS_ON, 0, out));
    lx_controls_block_free(blocked);
    cJSON_Delete(restricted);
    cJSON_Delete(source);
    free(data);
    cJSON_Delete(catalog);
    puts("PASS: seven typed controls, eight config types, locks/ranges, latest dimmer value, real timer timestamps, monitor masks, gate direction and window commands");
    return 0;
}
