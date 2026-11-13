/* Valódi modell: struktúra, típusok, parancsok, deltaállapot, korlátozás. */
#include "app_loxone_lighting.h"
#include "app_loxone_home_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *id = "10000000-0000-0000-0000000000000001";
static void event(lx_light_block_t *block, unsigned uuid, const char *text)
{
    char state[37];
    snprintf(state, sizeof(state), "10000000-0000-0000-%016x", uuid);
    lx_light_block_event(block, state, false, 0, text, strlen(text), 1000000);
}
static void number_event(lx_light_block_t *block, unsigned uuid, double value)
{
    char state[37];
    snprintf(state, sizeof(state), "10000000-0000-0000-%016x", uuid);
    lx_light_block_event(block, state, true, value, NULL, 0, 1000000);
}
static void numeric_packet(uint8_t out[24], unsigned uuid, double value)
{
    memset(out, 0, 24);
    out[3] = 0x10;
    out[15] = (uint8_t)uuid;
    memcpy(out + 16, &value, 8);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb"); assert(file);
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    char *source = calloc((size_t)length + 1, 1); assert(source);
    assert(fread(source, 1, (size_t)length, file) == (size_t)length); fclose(file);
    cJSON *catalog = lx_catalog_parse(source, (size_t)length); assert(catalog);
    const cJSON *entry = lx_light_catalog_find(catalog, id); assert(entry);
    lx_light_block_t *block = lx_light_block_create(entry); assert(block);
    assert(block->circuit_count == 6); /* A két master nem világítási kör. */
    assert(!strcmp(block->circuits[0].name, "Mennyezeti lámpák"));
    assert(block->circuits[0].type == LX_LIGHT_DIMMER);
    assert(block->circuits[1].type == LX_LIGHT_SWITCH);
    assert(block->circuits[2].type == LX_LIGHT_COLOR && block->circuits[2].rgb && block->circuits[2].white);
    assert(block->circuits[2].min_kelvin == 2200 && block->circuits[2].max_kelvin == 8000);
    assert(!block->circuits[3].rgb && block->circuits[3].white);
    assert(block->circuits[3].min_kelvin == 2700 && block->circuits[3].max_kelvin == 6500);
    assert(block->circuits[4].blocked);
    assert(block->circuits[5].type == LX_LIGHT_UNSUPPORTED);
    assert(lx_light_action_valid("10000000-0000-0000-0000000000000001/AI1"));
    assert(!lx_light_action_valid("10000000-0000-0000-0000000000000001/../off"));
    assert(!lx_light_action_valid("10000000-0000-0000-0000000000000001/AI1?value=1"));
    assert(!lx_light_action_valid("garbage/AI1"));
    lx_light_summary_t summary;
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_UNKNOWN);
    event(block, 4, "[]"); lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_UNKNOWN);
    number_event(block, 101, 42); lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_CUSTOM);
    event(block, 5, "[{\"id\":1,\"name\":\"TV nézés\"},{\"id\":2,\"name\":\"Munka\"}]");
    event(block, 4, "[1]"); lx_light_summary(block, &summary);
    assert(summary.label == LX_LIGHT_ONE_MOOD && !strcmp(summary.name, "TV nézés"));
    event(block, 4, "[0,1,1,2,3]"); lx_light_summary(block, &summary);
    assert(summary.label == LX_LIGHT_MANY_MOODS && summary.active_count == 3);
    event(block, 4, "[778]"); lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_CUSTOM);
    event(block, 4, "[NaN]"); assert(!block->active_known);
    event(block, 6, "{\"AI1\":\"Új dimmer név\",\"10000000-0000-0000-0000000000000001/AI3\":\"Új színkör\"}");
    assert(!strcmp(block->circuits[0].name, "Új dimmer név"));
    assert(!strcmp(block->circuits[2].name, "Új színkör"));
    lx_light_value_t color = {.kelvin = 3000};
    assert(lx_light_color_parse("hsv(120,50,80)", 14, &color));
    assert(color.mode == LX_LIGHT_MODE_HSV && color.hue == 120 && color.saturation == 50 && color.brightness == 80);
    assert(lx_light_color_parse("temp(35,4483)", 13, &color));
    assert(color.mode == LX_LIGHT_MODE_WHITE && color.kelvin == 4483 && color.brightness == 35 && color.hue == 120);
    assert(!lx_light_color_parse("hsv(nan,50,80)", 14, &color));
    assert(!lx_light_color_parse("hsv(1,2,3)/on", 13, &color));
    event(block, 103, "hsv(20,100,60)");
    char command[LX_LIGHT_COMMAND_SIZE];
    color = block->circuits[2].actual; color.brightness = 75;
    assert(lx_light_command_build(block, 2, &color, command));
    assert(strstr(command, "/AI3/hsv(20,100,75)"));
    color.mode = LX_LIGHT_MODE_WHITE; color.kelvin = 2100;
    assert(!lx_light_command_build(block, 2, &color, command));
    color.kelvin = 2200;
    assert(lx_light_command_build(block, 2, &color, command));
    assert(strstr(command, "/AI3/temp(75,2200)"));
    color = block->circuits[0].actual; color.brightness = 60;
    assert(lx_light_queue(block, 0, &color, 1000000));
    assert(lx_light_take(block, 0, 1000000, command) && strstr(command, "/AI1/60"));
    color.brightness = 70; assert(lx_light_queue(block, 0, &color, 1010000));
    color.brightness = 80; assert(lx_light_queue(block, 0, &color, 1020000));
    assert(!lx_light_take(block, 0, 1099999, command));
    assert(lx_light_take(block, 0, 1100000, command) && strstr(command, "/AI1/80"));
    assert(!lx_light_take(block, 0, 1200000, command));
    lx_light_circuit_t snapshot;
    lx_light_circuit_snapshot(block, 0, 1200000, &snapshot);
    assert(snapshot.actual.brightness == 80 && snapshot.pending);
    number_event(block, 101, 60); /* Késői korábbi visszajelzés. */
    lx_light_circuit_snapshot(block, 0, 1250000, &snapshot); assert(snapshot.actual.brightness == 80);
    number_event(block, 101, 80); assert(!block->circuits[0].pending);
    color.brightness = 0; assert(lx_light_queue(block, 0, &color, 1300000));
    lx_light_block_cancel(block); assert(!lx_light_take(block, 0, 1400000, command));
    number_event(block, 101, NAN); assert(!block->circuits[0].actual.valid);
    assert(!lx_light_queue(block, 0, &color, 1500000));
    for (unsigned i = 0; i < block->circuit_count; ++i)
        block->circuits[i].actual = (lx_light_value_t){.valid = true};
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_OFF);
    /* Read-only és ismeretlen típus nem vezérelhető, akkor sem, ha van állapota. */
    assert(!lx_light_command_build(block, 4, &color, command));
    assert(!lx_light_command_build(block, 5, &color, command));
    lx_home_config_t *home = calloc(1, sizeof(*home)); assert(home);
    home->card_count = 1; home->cards[0].type = LX_HOME_CARD_LIGHTING;
    strcpy(home->cards[0].id, "light"); strcpy(home->cards[0].control_uuid, id);
    assert(lx_home_command_build(home, catalog, "light", LX_HOME_COMMAND_LIGHT_NEXT, command) && strstr(command, "/plus"));
    assert(lx_home_command_build(home, catalog, "light", LX_HOME_COMMAND_LIGHT_PREV, command) && strstr(command, "/minus"));
    assert(lx_home_command_build(home, catalog, "light", LX_HOME_COMMAND_LIGHT_OFF, command) && strstr(command, "/changeTo/0"));
    assert(!lx_home_command_build(home, catalog, "light", LX_HOME_COMMAND_PULSE, command));
    strcpy(home->cards[0].name, "Világítás"); strcpy(home->cards[0].icon, "lightbulb");
    cJSON *saved = lx_home_config_json(home); assert(saved);
    lx_home_config_t *loaded = calloc(1, sizeof(*loaded)); assert(loaded);
    char error[LX_HOME_ERROR_SIZE]; assert(lx_home_config_parse(saved, loaded, error));
    assert(loaded->cards[0].type == LX_HOME_CARD_LIGHTING && !loaded->cards[0].source.uuid[0]);
    cJSON_Delete(saved); free(loaded);
    /* A rejtett lap nem követ adatot; a fogyasztók ugyanazon UUID-n élnek tovább. */
    home->card_count = 2; home->page_count = 2;
    home->pages[0].card_count = home->pages[1].card_count = 1; home->pages[1].first_card = 1;
    for (unsigned i = 0; i < 2; ++i)
    {
        home->cards[i].type = LX_HOME_CARD_VALUE;
        strcpy(home->cards[i].source.uuid, "10000000-0000-0000-0000000000000065");
        home->cards[i].source.scale = 1;
    }
    home->consumer_count = 1; home->consumers[0].source = home->cards[0].source;
    lx_home_values_t values = {0}; uint8_t packet[24]; numeric_packet(packet, 101, 10);
    assert(lx_home_values_update_visible(&values, home, -1, 2, packet, sizeof(packet)));
    assert(values.consumers[0].valid && !values.cards[0].valid && !values.cards[1].valid);
    assert(lx_home_values_update_visible(&values, home, 1, 2, packet, sizeof(packet)));
    assert(values.cards[1].valid && !values.cards[0].valid);
    assert(!lx_home_values_update_visible(&values, home, 8, 2, packet, sizeof(packet)));
    assert(!lx_home_values_update_visible(&values, home, 1, 2, packet, 23));
    char unicode_names[256] = "{\"AI1\":\"X";
    for (unsigned i = 0; i < 40; ++i) strcat(unicode_names, "é");
    strcat(unicode_names, "\"}");
    event(block, 6, unicode_names);
    assert(strlen(block->circuits[0].name) == LX_LIGHT_NAME_SIZE - 2);
    assert((unsigned char)block->circuits[0].name[LX_LIGHT_NAME_SIZE - 3] == 0xA9);
    free(home);
    /* A kártyaállapot a teljes körmodell töredékét foglalja. */
    assert(sizeof(lx_light_observation_t) * 4 < sizeof(lx_light_circuit_t));
    assert(lx_light_block_minimize(block));
    assert(!block->circuits && block->observations);
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_OFF);
    number_event(block, 101, 42);
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_CUSTOM);
    event(block, 4, "[1]");
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_ONE_MOOD);
    event(block, 4, "[1,2]");
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_MANY_MOODS);
    assert(!lx_light_command_build(block, 0, &color, command));
    assert(!lx_light_take(block, 0, 2000000, command));
    lx_light_block_cancel(block);
    lx_light_circuit_snapshot(block, 0, 2000000, &snapshot);
    assert(!snapshot.actual.valid);
    event(block, 6, "{\"AI1\":\"Zárt kártya\"}");
    assert(lx_light_block_minimize(block));
    printf("Lighting memory: compact circuit %zu, detailed circuit %zu bytes\n",
           sizeof(lx_light_observation_t), sizeof(lx_light_circuit_t));
    lx_light_block_free(block); cJSON_Delete(catalog); free(source);
    puts("Lighting model, state, command, rate and visible-page tests: OK");
    return 0;
}
