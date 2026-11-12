/* Az app_loxone.c része. A hálózat, a katalógusolvasás és a nagy foglalások
 * kizárólag a meglévő Loxone-workerben futnak. A GUI csak rövid mutexelt
 * kéréseket ad át; egy oldal és egy részletes blokk élhet egyszerre. */
#include "app_loxone_lighting_service.h"

typedef struct
{
    int page;
    unsigned generation, revision, first, count;
    lx_light_block_t *blocks[LX_HOME_MAX_CARDS];
} light_page_t;

static int s_light_page = -1;
static unsigned s_light_generation = 1, s_light_built_generation, s_light_built_revision;
static unsigned s_light_next_handle, s_light_modal, s_light_modal_card, s_light_detail_handle;
static unsigned s_light_send_cursor;
static light_page_t *s_light_context;
static lx_light_block_t *s_light_detail;
static bool s_light_initial_ready, s_light_resync, s_light_detail_failed;
static int64_t s_light_last_refresh;

static bool lighting_live_locked(void)
{
    return s_home_live && !s_dead && !s_home_busy &&
           esp_timer_get_time() - s_home_response < LX_KEEPALIVE_US * 3;
}

static bool lighting_current_locked(void)
{
    return s_home && s_light_context && s_light_page >= 0 &&
           s_light_context->page == s_light_page &&
           s_light_context->generation == s_light_generation &&
           s_light_context->revision == s_home_revision;
}

static void lighting_page_free(light_page_t *page)
{
    if (!page) return;
    for (unsigned i = 0; i < page->count; ++i)
    {
        bool shared = false;
        for (unsigned j = 0; j < i; ++j)
            if (page->blocks[j] == page->blocks[i]) shared = true;
        if (!shared) lx_light_block_free(page->blocks[i]);
    }
    free(page);
}

/* A hívó a kliensmutex tulajdonosa. Kapcsolatvesztéskor semmilyen elavult
 * parancs vagy ablakazonosító nem élheti túl a munkamenetet. */
static void lighting_invalidate_locked(void)
{
    ++s_light_generation;
    s_light_modal = s_light_detail_handle = 0;
    s_light_initial_ready = s_light_resync = s_light_detail_failed = false;
    s_light_last_refresh = 0;
    lighting_page_free(s_light_context);
    s_light_context = NULL;
    lx_light_block_free(s_light_detail);
    s_light_detail = NULL;
}

void app_loxone_home_visible(int page)
{
    if (!s_lock || !s_home) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned first, count;
    if (page < 0 || !lx_home_page_range(s_home, (unsigned)page, &first, &count))
        page = -1;
    if (page != s_light_page)
    {
        s_light_page = page;
        ++s_light_generation;
        s_light_modal = 0;
        s_light_initial_ready = false;
        lx_light_block_cancel(s_light_detail);
        /* A rejtett lap értékeit nem tekintjük frissnek újranyitáskor. */
        memset(s_home_values.cards, 0, sizeof(s_home_values.cards));
        memset(s_home_values.command_supported, 0, sizeof(s_home_values.command_supported));
    }
    xSemaphoreGive(s_lock);
}

static int lighting_slot_locked(const char *card_id)
{
    if (!card_id || !lighting_current_locked()) return -1;
    for (unsigned i = 0; i < s_light_context->count; ++i)
        if (!strcmp(s_home->cards[s_light_context->first + i].id, card_id))
            return (int)i;
    return -1;
}

bool app_loxone_light_summary(const char *card_id, unsigned revision, lx_light_summary_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = revision == s_home_revision ? lighting_slot_locked(card_id) : -1;
    bool valid = slot >= 0 && lighting_live_locked();
    if (valid)
    {
        lx_light_summary(s_light_context->blocks[slot], out);
        out->controllable = out->controllable && s_light_initial_ready;
    }
    xSemaphoreGive(s_lock);
    return valid;
}

unsigned app_loxone_light_open(const char *card_id, unsigned revision)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int slot = revision == s_home_revision ? lighting_slot_locked(card_id) : -1;
    unsigned handle = 0;
    if (slot >= 0 && lighting_live_locked() && s_light_context->blocks[slot])
    {
        lx_light_block_cancel(s_light_detail);
        if (++s_light_next_handle == 0) ++s_light_next_handle;
        handle = s_light_modal = s_light_next_handle;
        s_light_modal_card = s_light_context->first + (unsigned)slot;
        s_light_detail_failed = false;
    }
    xSemaphoreGive(s_lock);
    return handle;
}

void app_loxone_light_close(unsigned handle)
{
    if (!s_lock || !handle) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (handle == s_light_modal)
    {
        s_light_modal = 0;
        lx_light_block_cancel(s_light_detail);
    }
    xSemaphoreGive(s_lock);
}

bool app_loxone_light_info(unsigned handle, app_loxone_light_info_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s_lock || !handle) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = handle == s_light_modal && lighting_current_locked() && lighting_live_locked();
    out->connected = valid;
    if (valid && handle == s_light_detail_handle)
    {
        out->ready = s_light_detail && s_light_initial_ready;
        out->failed = s_light_detail_failed;
        out->circuit_count = s_light_detail ? s_light_detail->circuit_count : 0;
    }
    xSemaphoreGive(s_lock);
    return valid;
}

bool app_loxone_light_circuit(unsigned handle, unsigned index, lx_light_circuit_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s_lock || !handle) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = handle == s_light_modal && handle == s_light_detail_handle &&
                 lighting_current_locked() && lighting_live_locked() &&
                 s_light_initial_ready && s_light_detail && index < s_light_detail->circuit_count;
    if (valid)
        lx_light_circuit_snapshot(s_light_detail, index, esp_timer_get_time(), out);
    xSemaphoreGive(s_lock);
    return valid;
}

esp_err_t app_loxone_light_set(unsigned handle, unsigned index, const lx_light_value_t *value)
{
    if (!s_lock || !handle || !value) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool allowed = handle == s_light_modal && handle == s_light_detail_handle &&
                   lighting_current_locked() && lighting_live_locked() && s_light_initial_ready &&
                   lx_light_queue(s_light_detail, index, value, esp_timer_get_time());
    if (allowed) s_light_detail_failed = false;
    xSemaphoreGive(s_lock);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* A normál kártyaparancsok is csak a látható lapról indulhatnak.
 * A kaputelefon ettől független, mindig elérhető vezérlés marad. */
static bool lighting_home_command_allowed_locked(const char *id)
{
    if (!strcmp(id, LX_INTERCOM_GATE_ID)) return true;
    int slot = lighting_slot_locked(id);
    if (slot < 0 || !s_light_initial_ready) return false;
    if (s_home->cards[s_light_context->first + (unsigned)slot].type != LX_HOME_CARD_LIGHTING)
        return true;
    lx_light_summary_t summary;
    lx_light_summary(s_light_context->blocks[slot], &summary);
    return summary.available && summary.controllable;
}

/* A kliensworker írja a katalógust és a konfigurációt, ezért ezeket a
 * felépítés alatt nem kell másolni vagy GUI-mutex alatt feldolgozni. */
static light_page_t *lighting_page_create(int page, unsigned generation, unsigned revision)
{
    if (page < 0) return NULL;
    light_page_t *next = lx_light_alloc(1, sizeof(*next));
    if (!next) return NULL;
    next->page = page;
    next->generation = generation;
    next->revision = revision;
    if (!lx_home_page_range(s_home, (unsigned)page, &next->first, &next->count))
    {
        free(next);
        return NULL;
    }
    for (unsigned i = 0; i < next->count; ++i)
    {
        const lx_home_card_t *card = &s_home->cards[next->first + i];
        if (card->type != LX_HOME_CARD_LIGHTING) continue;
        bool shared = false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(s_home->cards[next->first + j].control_uuid, card->control_uuid) &&
                s_home->cards[next->first + j].type == LX_HOME_CARD_LIGHTING)
            {
                next->blocks[i] = next->blocks[j];
                shared = true;
                break;
            }
        if (shared) continue;
        lx_light_block_t *block = lx_light_block_create(lx_light_catalog_find(s_catalog, card->control_uuid));
        if (block && !lx_light_block_minimize(block))
        {
            lx_light_block_free(block);
            block = NULL;
        }
        next->blocks[i] = block;
    }
    return next;
}

static void lighting_reconcile(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned generation = s_light_generation, revision = s_home_revision;
    int page = s_light_page;
    bool change = s_light_built_generation != generation || s_light_built_revision != revision;
    light_page_t *old = NULL;
    lx_light_block_t *old_detail = NULL;
    if (change)
    {
        old = s_light_context;
        old_detail = s_light_detail;
        s_light_context = NULL;
        s_light_detail = NULL;
        s_light_detail_handle = 0;
        s_light_initial_ready = false;
    }
    xSemaphoreGive(s_lock);
    lighting_page_free(old);
    lx_light_block_free(old_detail);
    if (change)
    {
        light_page_t *next = lighting_page_create(page, generation, revision);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (generation == s_light_generation && revision == s_home_revision && !s_dead)
        {
            s_light_context = next;
            next = NULL;
            s_light_built_generation = generation;
            s_light_built_revision = revision;
            s_light_resync = page >= 0 && s_light_context;
        }
        xSemaphoreGive(s_lock);
        lighting_page_free(next);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned handle = s_light_modal;
    bool detail_change = handle != s_light_detail_handle;
    char control[LX_UUID_LEN] = "";
    old_detail = NULL;
    if (detail_change)
    {
        old_detail = s_light_detail;
        s_light_detail = NULL;
        if (handle && lighting_current_locked() && s_light_modal_card < s_home->card_count)
            snprintf(control, sizeof(control), "%s", s_home->cards[s_light_modal_card].control_uuid);
    }
    xSemaphoreGive(s_lock);
    lx_light_block_free(old_detail);
    if (detail_change)
    {
        lx_light_block_t *next = control[0] ?
            lx_light_block_create(lx_light_catalog_find(s_catalog, control)) : NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (handle == s_light_modal && generation == s_light_generation && revision == s_home_revision && !s_dead)
        {
            s_light_detail = next;
            next = NULL;
            s_light_detail_handle = handle;
            s_light_detail_failed = handle && !s_light_detail;
            s_light_send_cursor = 0;
            if (s_light_detail)
            {
                s_light_initial_ready = false;
                s_light_resync = true;
            }
        }
        xSemaphoreGive(s_lock);
        lx_light_block_free(next);
    }
}

static void lighting_event_locked(const char *uuid, bool numeric, double value,
                                  const char *text, size_t length, int64_t now)
{
    if (!lighting_current_locked()) return;
    for (unsigned i = 0; i < s_light_context->count; ++i)
    {
        bool shared = false;
        for (unsigned j = 0; j < i; ++j)
            if (s_light_context->blocks[j] == s_light_context->blocks[i]) shared = true;
        if (!shared)
            lx_light_block_event(s_light_context->blocks[i], uuid, numeric, value, text, length, now);
    }
    if (s_light_modal && s_light_modal == s_light_detail_handle)
        lx_light_block_event(s_light_detail, uuid, numeric, value, text, length, now);
}

static bool lighting_events_locked(int type, const uint8_t *data, size_t size)
{
    if (type != 2 && type != 3) return true;
    if (!data && size) return false;
    int64_t now = esp_timer_get_time();
    size_t position = 0;
    while (position < size)
    {
        size_t header = type == 2 ? 24 : 36;
        if (size - position < header) return false;
        char uuid[LX_UUID_LEN];
        lx_uuid_string(data + position, uuid);
        if (type == 2)
        {
            uint64_t bits = 0;
            for (unsigned i = 0; i < 8; ++i)
                bits |= (uint64_t)data[position + 16 + i] << (8 * i);
            double value;
            memcpy(&value, &bits, sizeof(value));
            lighting_event_locked(uuid, true, value, NULL, 0, now);
            position += 24;
        }
        else
        {
            uint32_t length = 0;
            for (unsigned i = 0; i < 4; ++i)
                length |= (uint32_t)data[position + 32 + i] << (8 * i);
            if (length > size - position - 36) return false;
            size_t padding = (4 - (length & 3)) & 3;
            if (padding > size - position - 36 - length) return false;
            lighting_event_locked(uuid, false, 0, (const char *)data + position + 36, length, now);
            position += 36 + length + padding;
        }
    }
    return true;
}

static void lighting_poll(int64_t now)
{
    lighting_reconcile();
    now = esp_timer_get_time();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool refresh = s_light_resync && lighting_current_locked() && lighting_live_locked() &&
                   (!s_light_last_refresh || now - s_light_last_refresh >= 250000);
    unsigned generation = s_light_generation;
    if (refresh)
    {
        s_light_resync = false;
        s_light_last_refresh = now;
    }
    xSemaphoreGive(s_lock);
    if (refresh)
    {
        /* A protokoll közös állapotfolyamot ad, nem UUID-nkénti előfizetést.
         * Új nézethez új kezdőállapotot kérünk, kapcsolatbontás nélkül;
         * az érkező táblából kizárólag a szükséges adatokat tartjuk meg. */
        cJSON *answer = reply("jdev/sps/enablebinstatusupdate", false);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (generation == s_light_generation)
        {
            s_light_initial_ready = answer != NULL && !s_dead;
            if (!answer) s_light_detail_failed = true;
        }
        xSemaphoreGive(s_lock);
        cJSON_Delete(answer);
    }

    char command[LX_LIGHT_COMMAND_SIZE] = "";
    unsigned handle = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    now = esp_timer_get_time();
    if (lighting_current_locked() && lighting_live_locked() && s_light_initial_ready &&
        s_light_modal && s_light_modal == s_light_detail_handle && s_light_detail)
    {
        unsigned count = s_light_detail->circuit_count;
        for (unsigned step = 0; step < count; ++step)
        {
            unsigned index = (s_light_send_cursor + step) % count;
            lx_light_circuit_t *circuit = &s_light_detail->circuits[index];
            /* Hosszú hálózati várakozás után nem játsszuk vissza az elavult húzást. */
            if (circuit->dirty && now > circuit->pending_until_us)
                circuit->dirty = circuit->pending = false;
            if (lx_light_take(s_light_detail, index, now, command))
            {
                handle = s_light_modal;
                s_light_send_cursor = (index + 1) % count;
                break;
            }
        }
    }
    xSemaphoreGive(s_lock);
    if (!handle) return;
    /* A meglévő soros kérés/válasz út megőrzi az ACK-k sorrendjét.
     * A nyugta önmagában nem helyettesíti a bináris állapot-visszajelzést. */
    cJSON *answer = reply(command, true);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!answer && handle == s_light_modal)
    {
        lx_light_block_cancel(s_light_detail);
        s_light_detail_failed = true;
    }
    xSemaphoreGive(s_lock);
    cJSON_Delete(answer);
}
