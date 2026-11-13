/* Az éles láthatólap-szolgáltatás és nyers cache host-hámmal. Nincs hálózat;
 * a szinkronizáció itt egy szálon fut, a valódi életciklus-kód változatlan. */
#include "app_loxone_home_runtime.h"
#include "app_loxone_controls_service.h"
#include "app_loxone_audio_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LX_KEEPALIVE_US 15000000LL
#define portMAX_DELAY 0
static int s_lock = 1;
static bool s_dead, s_home_busy, s_command_pending, s_home_live = true;
static unsigned s_home_revision = 1;
static int64_t now_us = 1000000, s_home_response = 1000000;
static lx_home_config_t *s_home;
static lx_home_values_t s_home_values;
static cJSON *s_catalog;
static unsigned sends;
static char sent[96];
static int audio_page = -1;

int64_t esp_timer_get_time(void) { return now_us; }
static void xSemaphoreTake(int lock, int wait) { (void)lock; (void)wait; }
static void xSemaphoreGive(int lock) { (void)lock; }
static cJSON *reply(const char *command, bool encrypted)
{
    assert(encrypted);
    snprintf(sent, sizeof(sent), "%s", command);
    ++sends;
    return cJSON_CreateObject();
}
void app_loxone_audio_hide(void) { audio_page = -1; }
void app_loxone_audio_page(const lx_home_config_t *home, const cJSON *catalog, int page, unsigned revision)
{
    assert(home == s_home && catalog == s_catalog && revision == s_home_revision);
    audio_page = page;
}
void app_loxone_audio_state(const char *uuid, bool numeric, double value,
                            const char *text, size_t length, int64_t now)
{
    (void)uuid; (void)numeric; (void)value; (void)text; (void)length; (void)now;
}
#include "../main/app_loxone_lighting_service.inc"

static void event_number(lx_controls_block_t *block, unsigned state, double value)
{
    lighting_event_locked(block->states[state], true, value, NULL, 0, now_us);
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
    s_catalog = lx_catalog_parse(data, (size_t)length);
    free(data);
    s_home = calloc(1, sizeof(*s_home));
    assert(s_home && s_catalog);
    s_home->page_count = 2;
    s_home->card_count = 3;
    s_home->pages[0].card_count = 2;
    s_home->pages[1].first_card = 2;
    s_home->pages[1].card_count = 1;
    for (unsigned i = 0; i < 3; ++i)
    {
        snprintf(s_home->cards[i].id, sizeof(s_home->cards[i].id), "card-%u", i);
        s_home->cards[i].type = i == 2 ? LX_HOME_CARD_TIMED_SWITCH : LX_HOME_CARD_DIMMER;
        snprintf(s_home->cards[i].control_uuid, sizeof(s_home->cards[i].control_uuid),
                 "40000000-0000-0000-000000000000000%u", i == 2 ? 4 : 1);
    }
    app_loxone_home_visible(0);
    lighting_poll(now_us);
    assert(lighting_current_locked() && audio_page == 0);
    assert(s_light_context->controls_blocks[0] == s_light_context->controls_blocks[1]);
    assert(!s_light_context->controls_blocks[2]);
    lx_controls_block_t *dimmer = s_light_context->controls_blocks[0];
    assert(!lighting_home_command_allowed_locked("card-0"));
    lighting_event_locked(dimmer->states[LX_CONTROL_LOCK], false, 0, "", 0, now_us);
    event_number(dimmer, LX_CONTROL_MIN, 0);
    event_number(dimmer, LX_CONTROL_MAX, 100);
    event_number(dimmer, LX_CONTROL_STEP, 1);
    event_number(dimmer, LX_CONTROL_VALUE, 25);
    assert(lighting_home_command_allowed_locked("card-0"));
    assert(lx_controls_queue(dimmer, 40, now_us));
    assert(lx_controls_queue(dimmer, 65, now_us));
    controls_poll(now_us);
    assert(sends == 1 && strstr(sent, "/65.000000"));
    lx_controls_summary_t summary;
    assert(app_loxone_controls_summary("card-1", 1, &summary) && summary.value == 25);
    /* A rejtett timernek csak a UUID-állapota él; nincs aktív blokkmodell. */
    lighting_event_locked("40000000-0000-0000-0000000000000fa1", true, 10, NULL, 0, now_us);
    lighting_event_locked("40000000-0000-0000-0000000000000fa3", false, 0, "", 0, now_us);
    now_us = 5500000;
    app_loxone_home_visible(1);
    assert(audio_page == -1);
    assert(!app_loxone_controls_summary("card-0", 1, &summary));
    lighting_poll(now_us);
    assert(audio_page == 1);
    assert(app_loxone_controls_summary("card-2", 1, &summary));
    assert(summary.timer_valid && summary.timer_seconds == 5.5 && summary.timer_running);
    assert(summary.controllable);
    /* Visszalapozáskor a kezdő dimmerérték megmarad, nem kell új snapshot. */
    app_loxone_home_visible(0);
    lighting_poll(now_us);
    assert(app_loxone_controls_summary("card-0", 1, &summary) && summary.value == 25);
    assert(summary.controllable);
    assert(!app_loxone_controls_summary("card-0", 2, &summary));
    app_loxone_home_visible(-1);
    lighting_poll(now_us);
    assert(!s_light_context && audio_page == -1);
    assert(lighting_events_locked(2, NULL, 0));
    lighting_invalidate_locked();
    assert(!s_light_cache.entries && !s_light_cache.count);
    free(s_home);
    cJSON_Delete(s_catalog);
    puts("PASS: actual shared service/cache, duplicated visible model, hidden timer timestamp, page/revision cancellation and cache cleanup");
    return 0;
}
