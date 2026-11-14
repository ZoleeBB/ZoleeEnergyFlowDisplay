/* A Miniserver egy kapcsolaton egyszer küld teljes kezdőállapotot, utána csak
 * változásokat. A rejtett oldalakat nem modellezzük, de a konfigurált kártyák
 * utolsó nyers értékeit meg kell őrizni. Ez nem parancssor: innen soha nem
 * küldünk vezérlést. Kizárólag a meglévő Loxone-worker használja. */
#define LIGHT_CACHE_MAX_STATES LX_CATALOG_MAX
#define LIGHT_CACHE_TEXT_BUDGET (64U * 1024U)

typedef struct
{
    char uuid[LX_UUID_LEN];
    char *text;
    size_t length;
    double number;
    int64_t received_us;
    bool seen, numeric;
} light_cache_entry_t;

static struct
{
    light_cache_entry_t *entries;
    unsigned count, capacity;
    size_t text_bytes;
    bool built, failed;
} s_light_cache;

static void lighting_cache_clear(void)
{
    for (unsigned i = 0; i < s_light_cache.count; ++i)
        free(s_light_cache.entries[i].text);
    free(s_light_cache.entries);
    memset(&s_light_cache, 0, sizeof(s_light_cache));
}

static const char *lighting_cache_string(const cJSON *object, const char *key)
{
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    return text ? text : "";
}

static bool lighting_cache_add(const char *uuid)
{
    if (!lx_uuid_valid(uuid))
        return true; /* Nem létező/opcionális állapot nem képez gyorsítótárat. */
    for (unsigned i = 0; i < s_light_cache.count; ++i)
        if (!strcmp(s_light_cache.entries[i].uuid, uuid))
            return true;
    if (s_light_cache.count == LIGHT_CACHE_MAX_STATES)
        return false;
    if (s_light_cache.count == s_light_cache.capacity)
    {
        unsigned capacity = s_light_cache.capacity ? s_light_cache.capacity * 2 : 16;
        if (capacity > LIGHT_CACHE_MAX_STATES)
            capacity = LIGHT_CACHE_MAX_STATES;
        light_cache_entry_t *next = lx_light_alloc(capacity, sizeof(*next));
        if (!next)
            return false; /* Nincs visszaesés a belső/DMA-heapre. */
        if (s_light_cache.count)
            memcpy(next, s_light_cache.entries, s_light_cache.count * sizeof(*next));
        free(s_light_cache.entries);
        s_light_cache.entries = next;
        s_light_cache.capacity = capacity;
    }
    light_cache_entry_t *entry = &s_light_cache.entries[s_light_cache.count++];
    memcpy(entry->uuid, uuid, LX_UUID_LEN);
    return true;
}

static int lighting_cache_compare(const void *a, const void *b)
{
    return strcmp(((const light_cache_entry_t *)a)->uuid,
                  ((const light_cache_entry_t *)b)->uuid);
}

static void lighting_cache_prepare(void)
{
    if (s_light_cache.built || !s_home || !s_catalog)
        return;
    s_light_cache.built = true;
    bool ok = true;
    for (unsigned i = 0; ok && i < s_home->card_count; ++i)
    {
        const lx_home_card_t *card = &s_home->cards[i];
        if (card->type != LX_HOME_CARD_LIGHTING && card->type != LX_HOME_CARD_IRC &&
            card->type != LX_HOME_CARD_SHADING && !lx_home_card_is_controls(card->type) &&
            card->type != LX_HOME_CARD_AUDIO)
        {
            if (card->type != LX_HOME_CARD_PULSE)
                ok = lighting_cache_add(card->source.uuid);
            continue;
        }
        bool duplicate = false;
        for (unsigned j = 0; j < i; ++j)
            if (s_home->cards[j].type == card->type &&
                !strcmp(s_home->cards[j].control_uuid, card->control_uuid))
                duplicate = true;
        if (duplicate)
            continue;
        const cJSON *entry = card->type == LX_HOME_CARD_AUDIO ?
            lx_audio_catalog_find(s_catalog, card->control_uuid) :
            lx_home_card_is_controls(card->type) ?
            lx_controls_catalog_find(s_catalog, card->control_uuid, card->type) :
            card->type == LX_HOME_CARD_IRC ?
            lx_irc_catalog_find(s_catalog, card->control_uuid) :
            card->type == LX_HOME_CARD_SHADING ?
            lx_shading_catalog_find(s_catalog, card->control_uuid) :
            lx_light_catalog_find(s_catalog, card->control_uuid);
        if (!entry)
            continue;
        const char *encoded = lighting_cache_string(entry,
            card->type == LX_HOME_CARD_IRC ? "irc" :
            card->type == LX_HOME_CARD_SHADING ? "shading" :
            lx_home_card_is_controls(card->type) ? "controls" :
            card->type == LX_HOME_CARD_AUDIO ? "audio" : "lighting");
        /* Saját, normalizált katalógusmetaadat; egyszerre csak egy blokk
         * átmeneti JSON-ja él. Nincs a veremben körlista vagy teljes profil. */
        size_t limit = card->type == LX_HOME_CARD_AUDIO ? LX_AUDIO_META_MAX :
                       lx_home_card_is_controls(card->type) ? LX_CONTROLS_META_MAX :
                       card->type == LX_HOME_CARD_IRC ? LX_IRC_META_MAX :
                       card->type == LX_HOME_CARD_SHADING ? LX_SHADING_META_MAX : LX_LIGHT_TEXT_MAX;
        cJSON *meta = strlen(encoded) <= limit ? cJSON_Parse(encoded) : NULL;
        if (!meta)
        {
            ok = false;
            break;
        }
        if (lx_home_card_is_controls(card->type) || card->type == LX_HOME_CARD_AUDIO)
        {
            const cJSON *state;
            cJSON_ArrayForEach(state, cJSON_GetObjectItemCaseSensitive(meta, "states"))
                if (ok && cJSON_IsString(state))
                    ok = lighting_cache_add(state->valuestring);
            cJSON_Delete(meta);
            continue;
        }
        if (card->type == LX_HOME_CARD_IRC)
        {
            /* Ugyanabba a nyers tárba kerül az IRC öt állapota. A jLocked
             * kezdeti üres üzenetét a rejtett kártyánál is meg kell őrizni. */
            static const char *const keys[] = {
                "target", "actual", "current_mode", "operating_mode", "lock"
            };
            for (unsigned j = 0; ok && j < sizeof(keys) / sizeof(keys[0]); ++j)
                ok = lighting_cache_add(lighting_cache_string(meta, keys[j]));
            cJSON_Delete(meta);
            continue;
        }
        if (card->type == LX_HOME_CARD_SHADING)
        {
            /* Egyetlen közös, korlátos nyers tár; a rejtett árnyékolóhoz
             * nincs blokkmodell vagy külön hálózati állapotlekérés. */
            static const char *const keys[] = {
                "lock", "locked", "device", "position", "up", "down", "safety"
            };
            for (unsigned j = 0; ok && j < sizeof(keys) / sizeof(keys[0]); ++j)
                ok = lighting_cache_add(lighting_cache_string(meta, keys[j]));
            cJSON_Delete(meta);
            continue;
        }
        ok = lighting_cache_add(lighting_cache_string(meta, "lock")) &&
             lighting_cache_add(lighting_cache_string(meta, "active")) &&
             lighting_cache_add(lighting_cache_string(meta, "moods")) &&
             lighting_cache_add(lighting_cache_string(meta, "names"));
        const cJSON *circuit;
        cJSON_ArrayForEach(circuit, cJSON_GetObjectItemCaseSensitive(meta, "circuits"))
        {
            if (!ok)
                break;
            ok = lighting_cache_add(lighting_cache_string(circuit, "lock")) &&
                 lighting_cache_add(lighting_cache_string(circuit, "state"));
        }
        cJSON_Delete(meta);
    }
    s_light_cache.failed = !ok;
    if (s_light_cache.count > 1)
        qsort(s_light_cache.entries, s_light_cache.count, sizeof(*s_light_cache.entries),
              lighting_cache_compare);
}

static light_cache_entry_t *lighting_cache_find(const char *uuid)
{
    unsigned low = 0, high = s_light_cache.count;
    while (low < high)
    {
        unsigned middle = low + (high - low) / 2;
        int order = strcmp(uuid, s_light_cache.entries[middle].uuid);
        if (!order)
            return &s_light_cache.entries[middle];
        if (order < 0)
            high = middle;
        else
            low = middle + 1;
    }
    return NULL;
}

static void lighting_cache_store(const char *uuid, bool numeric, double value,
                                 const char *text, size_t length)
{
    light_cache_entry_t *entry = lighting_cache_find(uuid);
    if (!entry)
        return;
    if (!numeric && entry->seen && !entry->numeric && entry->length == length &&
        (!length || (text && !memcmp(entry->text, text, length))))
        return;
    /* Előbb érvénytelenítünk: foglalási hibánál sem maradhat régi, feloldott
     * jLocked állapot. Az üres szöveg viszont valódi, ismert állapot. */
    entry->seen = false;
    if (entry->text)
    {
        s_light_cache.text_bytes -= entry->length + 1;
        free(entry->text);
        entry->text = NULL;
    }
    entry->length = 0;
    entry->numeric = numeric;
    if (!numeric && length)
    {
        if (!text || length > LX_LIGHT_TEXT_MAX ||
            length + 1 > LIGHT_CACHE_TEXT_BUDGET - s_light_cache.text_bytes)
        {
            s_light_cache.failed = true;
            return;
        }
        entry->text = lx_light_alloc(length + 1, 1);
        if (!entry->text)
        {
            s_light_cache.failed = true;
            return;
        }
        memcpy(entry->text, text, length);
        entry->length = length;
        s_light_cache.text_bytes += length + 1;
    }
    entry->number = value;
    entry->received_us = esp_timer_get_time();
    entry->seen = true;
}

static void lighting_cache_replay_block(lx_light_block_t *block)
{
    if (!block)
        return;
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < s_light_cache.count; ++i)
    {
        const light_cache_entry_t *entry = &s_light_cache.entries[i];
        if (entry->seen)
            lx_light_block_event(block, entry->uuid, entry->numeric, entry->number,
                                 entry->text, entry->length, now);
    }
}

static void lighting_cache_replay_irc(lx_irc_block_t *block)
{
    if (!block)
        return;
    const char *const states[] = {
        block->target_state, block->actual_state, block->current_mode_state,
        block->operating_mode_state, block->lock_state
    };
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        bool shared = false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(states[j], states[i]))
                shared = true;
        if (shared)
            continue;
        light_cache_entry_t *entry = lighting_cache_find(states[i]);
        if (entry && entry->seen)
            lx_irc_block_event(block, entry->uuid, entry->numeric, entry->number,
                                entry->text, entry->length, now);
    }
}

static void lighting_cache_replay_shading(lx_shading_block_t *block)
{
    if (!block)
        return;
    const char *const states[] = {
        block->position_state, block->up_state, block->down_state,
        block->locked_state, block->safety_state, block->device_state, block->lock_state
    };
    int64_t now = esp_timer_get_time();
    for (unsigned i = 0; i < sizeof(states) / sizeof(states[0]); ++i)
    {
        bool shared = false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(states[j], states[i]))
                shared = true;
        if (shared)
            continue;
        light_cache_entry_t *entry = lighting_cache_find(states[i]);
        if (entry && entry->seen)
            lx_shading_block_event(block, entry->uuid, entry->numeric, entry->number,
                                    entry->text, entry->length, now);
    }
}

/* Az időzített kapcsoló a tényleges beérkezés idejéből számol. Lapváltás
 * nem indíthatja újra a korábban megkapott visszaszámlálást. */
static void lighting_cache_replay_controls(lx_controls_block_t *block)
{
    if (!block)
        return;
    for (unsigned i = 0; i < LX_CONTROLS_STATE_COUNT; ++i)
    {
        light_cache_entry_t *entry = lighting_cache_find(block->states[i]);
        if (entry && entry->seen)
            lx_controls_block_event(block, entry->uuid, entry->numeric, entry->number,
                                      entry->text, entry->length, entry->received_us);
    }
}

static unsigned lighting_cache_hex(char ch)
{
    return ch >= '0' && ch <= '9' ? (unsigned)(ch - '0') :
           (unsigned)((ch | 32) - 'a' + 10);
}

static void lighting_cache_replay_page(int page)
{
    if (page < 0)
        return;
    /* A valódi Otthon-feldolgozót használjuk, nem másoljuk az értékellenőrzést.
     * A GUID első három mezője little endian, az utolsó nyolc bájt változatlan. */
    static const uint8_t positions[16] = {6, 4, 2, 0, 11, 9, 16, 14,
                                         19, 21, 23, 25, 27, 29, 31, 33};
    for (unsigned i = 0; i < s_light_cache.count; ++i)
    {
        const light_cache_entry_t *entry = &s_light_cache.entries[i];
        if (!entry->seen || !entry->numeric)
            continue;
        uint8_t packet[24];
        for (unsigned byte = 0; byte < 16; ++byte)
        {
            unsigned p = positions[byte];
            packet[byte] = (uint8_t)((lighting_cache_hex(entry->uuid[p]) << 4) |
                                      lighting_cache_hex(entry->uuid[p + 1]));
        }
        uint64_t bits;
        memcpy(&bits, &entry->number, sizeof(bits));
        for (unsigned byte = 0; byte < 8; ++byte)
            packet[16 + byte] = (uint8_t)(bits >> (8 * byte));
        (void)lx_home_values_update_visible(&s_home_values, s_home, page, 2,
                                            packet, sizeof(packet));
    }
}
