/* A platformfüggetlen Loxone-modell része; a meglévő model.c fordítási
 * egységbe kerül, ezért a Master, Slave, host-tesztek és szimulátor azonos
 * parancsellenőrzést használ. Nincs külön feladat vagy állandó képpuffer. */
#include "app_loxone_lighting.h"
#include <limits.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

void *lx_light_alloc(size_t count, size_t size)
{
    if (!count || !size || count > SIZE_MAX / size)
        return NULL;
#ifdef ESP_PLATFORM
    /* A világítási felület opcionális: RAM-hiányban nem veszi el a DMA-heapet. */
    return heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return calloc(count, size);
#endif
}

static const char *light_string(const cJSON *object, const char *key)
{
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, key));
    return value ? value : "";
}

static void light_copy(char *out, size_t capacity, const char *source)
{
    size_t size = strlen(source);
    if (size >= capacity)
    {
        size = capacity - 1;
        /* UTF-8 folytatóbájt nem maradhat a rövidítés szélén. */
        while (size && ((unsigned char)source[size] & 0xC0) == 0x80)
            --size;
    }
    memcpy(out, source, size);
    out[size] = 0;
}

bool lx_light_action_valid(const char *action)
{
    if (!action || strlen(action) >= LX_LIGHT_ACTION_SIZE)
        return false;
    const char *slash = strchr(action, '/');
    if (!slash)
        return lx_uuid_valid(action);
    size_t base = (size_t)(slash - action);
    char uuid[LX_UUID_LEN];
    if (!base || base >= sizeof(uuid) || !slash[1])
        return false;
    memcpy(uuid, action, base);
    uuid[base] = 0;
    if (!lx_uuid_valid(uuid) || strlen(slash + 1) > 20)
        return false;
    for (const char *p = slash + 1; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-'))
            return false;
    return true;
}

static bool light_blocked(const cJSON *control, bool inherited)
{
    const cJSON *secure = cJSON_GetObjectItemCaseSensitive(control, "isSecured");
    const cJSON *restrictions = cJSON_GetObjectItemCaseSensitive(control, "restrictions");
    /* Helyi kapcsolat: a read-only belső bitje (#1). Ismeretlen hibás
     * korlátozás esetén a biztonságos, csak olvasható megjelenítés marad. */
    bool read_only =
        restrictions && (!cJSON_IsNumber(restrictions) || !isfinite(restrictions->valuedouble) ||
                         restrictions->valuedouble < 0 || restrictions->valuedouble > INT_MAX ||
                         restrictions->valuedouble != floor(restrictions->valuedouble) ||
                         ((unsigned)restrictions->valuedouble & 2U));
    return inherited || read_only ||
           (secure &&
            !(cJSON_IsFalse(secure) || (cJSON_IsNumber(secure) && secure->valuedouble == 0)));
}

static bool light_state_add(cJSON *dest, const char *key, const cJSON *states, const char *state)
{
    const char *uuid = light_string(states, state);
    return cJSON_AddStringToObject(dest, key, lx_uuid_valid(uuid) ? uuid : "") != NULL;
}

static bool light_master(const char *reference, const char *key, const char *action)
{
    if (!reference[0])
        return false;
    if (!strcmp(reference, key) || !strcmp(reference, action))
        return true;
    const char *tail = strrchr(action, '/');
    return tail && !strcmp(reference, tail + 1);
}

bool lx_light_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured)
{
    if (strcmp(light_string(control, "type"), "LightControllerV2"))
        return true;
    const char *parent = light_string(control, "uuidAction");
    if (!parent[0] && !cJSON_GetObjectItemCaseSensitive(control, "uuidAction"))
        parent = control->string ? control->string : "";
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(control, "states");
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(control, "details");
    bool blocked = light_blocked(control, parent_secured) || !lx_uuid_valid(parent);
    cJSON *meta = cJSON_CreateObject(), *circuits = cJSON_CreateArray();
    bool ok = meta && circuits && cJSON_AddItemToObject(meta, "circuits", circuits);
    if (!ok)
    {
        cJSON_Delete(meta);
        cJSON_Delete(circuits);
        return false;
    }
    ok = cJSON_AddStringToObject(meta, "action", parent) &&
         cJSON_AddBoolToObject(meta, "blocked", blocked) &&
         light_state_add(meta, "active", states, "activeMoods") &&
         light_state_add(meta, "moods", states, "moodList") &&
         light_state_add(meta, "names", states, "circuitNames") &&
         light_state_add(meta, "lock", states, "jLocked");
    const cJSON *child;
    cJSON_ArrayForEach(child, cJSON_GetObjectItemCaseSensitive(control, "subControls"))
    {
        if (!ok)
            break;
        const char *key = child->string ? child->string : "";
        const char *explicit_action = light_string(child, "uuidAction");
        char action[LX_LIGHT_ACTION_SIZE] = "";
        if (cJSON_GetObjectItemCaseSensitive(child, "uuidAction"))
        {
            if (lx_light_action_valid(explicit_action))
                light_copy(action, sizeof(action), explicit_action);
        }
        else if (lx_light_action_valid(key))
            light_copy(action, sizeof(action), key);
        else
        {
            size_t parent_length = strlen(parent), key_length = strlen(key);
            /* A perjel és a lezáró nulla is férjen el. Kivonással ellenőrzünk,
             * így a hosszösszeg sem csordulhat túl. Nincs csonkolt parancscím,
             * nagyobb puffer vagy variadikus formázóhívás a veremláncban. */
            if (parent_length < sizeof(action) - 1 &&
                key_length < sizeof(action) - parent_length - 1)
            {
                memcpy(action, parent, parent_length);
                action[parent_length] = '/';
                memcpy(action + parent_length + 1, key, key_length + 1);
                if (!lx_light_action_valid(action))
                    action[0] = 0;
            }
        }
        if (light_master(light_string(details, "masterValue"), key, action) ||
            light_master(light_string(details, "masterColor"), key, action))
            continue;
        /* A gyártói üres type kifejezetten nem megjelenítendő objektum. */
        const char *type = light_string(child, "type");
        if (!type[0])
            continue;
        if (cJSON_GetArraySize(circuits) >= LX_LIGHT_MAX_CIRCUITS)
        {
            /* Nem csonkoljuk és nem mutatjuk félkésznek a blokkot. */
            ok = cJSON_AddBoolToObject(meta, "too_large", true) != NULL;
            break;
        }
        cJSON *item = cJSON_CreateObject();
        if (!item)
        {
            ok = false;
            break;
        }
        const cJSON *child_states = cJSON_GetObjectItemCaseSensitive(child, "states");
        const cJSON *child_details = cJSON_GetObjectItemCaseSensitive(child, "details");
        lx_light_type_t kind = !strcmp(type, "Switch")        ? LX_LIGHT_SWITCH
                             : !strcmp(type, "Dimmer")        ? LX_LIGHT_DIMMER
                             : !strcmp(type, "ColorPickerV2") ? LX_LIGHT_COLOR
                                                              : LX_LIGHT_UNSUPPORTED;
        const char *state = kind == LX_LIGHT_SWITCH ? "active"
                          : kind == LX_LIGHT_DIMMER ? "position"
                                                    : "color";
        const char *picker = light_string(child_details, "pickerType");
        const cJSON *min = cJSON_GetObjectItemCaseSensitive(child_details, "TWMin");
        const cJSON *max = cJSON_GetObjectItemCaseSensitive(child_details, "TWMax");
        double low = min ? (cJSON_IsNumber(min) ? min->valuedouble : 0) : 2700;
        double high = max ? (cJSON_IsNumber(max) ? max->valuedouble : 0) : 6500;
        bool range_ok = isfinite(low) && isfinite(high) && low >= 1000 && high <= 40000 &&
                        low <= high && low == floor(low) && high == floor(high);
        bool rgb = !strcmp(picker, "Rgb") || !strcmp(picker, "Lumitech");
        bool white = rgb || !strcmp(picker, "TunableWhite");
        /* Régi konfigurációban hiányzó pickerType: a támogatás nem találgatható. */
        ok = cJSON_AddStringToObject(item, "id", key) &&
             cJSON_AddStringToObject(item, "action", action) &&
             cJSON_AddStringToObject(item, "name", light_string(child, "name")) &&
             cJSON_AddNumberToObject(item, "type", kind) &&
             cJSON_AddBoolToObject(item, "blocked", light_blocked(child, blocked) || !action[0]) &&
             light_state_add(item, "state", child_states, state) &&
             light_state_add(item, "lock", child_states, "jLocked") &&
             cJSON_AddBoolToObject(item, "rgb", rgb) &&
             cJSON_AddBoolToObject(item, "white", white && range_ok) &&
             cJSON_AddNumberToObject(item, "min", range_ok ? low : 2700) &&
             cJSON_AddNumberToObject(item, "max", range_ok ? high : 6500);
        if (!ok || !cJSON_AddItemToArray(circuits, item))
        {
            cJSON_Delete(item);
            ok = false;
        }
    }
    /* A subControls sorrendjét változtatás nélkül tartjuk: nincs név szerinti rendezés. */
    char *compact = ok ? cJSON_PrintUnformatted(meta) : NULL;
    ok = compact && strlen(compact) <= LX_LIGHT_TEXT_MAX &&
         cJSON_AddBoolToObject(entry, "lighting_blocked", blocked) &&
         cJSON_AddStringToObject(entry, "lighting", compact);
    free(compact);
    cJSON_Delete(meta);
    return ok;
}

const cJSON *lx_light_catalog_find(const cJSON *catalog, const char *control_uuid)
{
    if (!control_uuid || !lx_uuid_valid(control_uuid))
        return NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(
        entry, catalog) if (!strcmp(light_string(entry, "control_uuid"), control_uuid) &&
                            !strcmp(light_string(entry, "type"), "LightControllerV2") &&
                            cJSON_IsString(
                                cJSON_GetObjectItemCaseSensitive(entry, "lighting"))) return entry;
    return NULL;
}

static cJSON *light_json(const char *text, size_t size)
{
    if (!text || !size || size > LX_LIGHT_TEXT_MAX || memchr(text, 0, size))
        return NULL;
    unsigned depth = 0;
    bool quoted = false, escape = false;
    for (size_t i = 0; i < size; ++i)
    {
        char ch = text[i];
        if (quoted)
        {
            if (escape)
                escape = false;
            else if (ch == '\\')
                escape = true;
            else if (ch == '"')
                quoted = false;
        }
        else if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[')
        {
            if (++depth > 8)
                return NULL;
        }
        else if (ch == '}' || ch == ']')
        {
            if (!depth)
                return NULL;
            --depth;
        }
        else if (!depth && i && !strchr(" \r\n\t", ch))
            return NULL;
    }
    if (depth || quoted)
        return NULL;
    /* A parser a megadott hosszon belül dolgozik, terminált másolat nélkül. */
    return cJSON_ParseWithLength(text, size);
}

lx_light_block_t *lx_light_block_create(const cJSON *entry)
{
    const char *encoded = light_string(entry, "lighting");
    cJSON *meta = light_json(encoded, strlen(encoded));
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(meta, "circuits");
    int count = cJSON_GetArraySize(items);
    if (!meta || !cJSON_IsArray(items) || count < 0 || count > LX_LIGHT_MAX_CIRCUITS ||
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(meta, "too_large")))
    {
        cJSON_Delete(meta);
        return NULL;
    }
    lx_light_block_t *block = lx_light_alloc(1, sizeof(*block));
    if (block && count)
        block->circuits = lx_light_alloc((unsigned)count, sizeof(*block->circuits));
    if (!block || (count && !block->circuits))
    {
        lx_light_block_free(block);
        cJSON_Delete(meta);
        return NULL;
    }
    light_copy(block->action, sizeof(block->action), light_string(meta, "action"));
    light_copy(block->active_state, sizeof(block->active_state), light_string(meta, "active"));
    light_copy(block->moods_state, sizeof(block->moods_state), light_string(meta, "moods"));
    light_copy(block->names_state, sizeof(block->names_state), light_string(meta, "names"));
    light_copy(block->lock_state, sizeof(block->lock_state), light_string(meta, "lock"));
    block->blocked = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(meta, "blocked"));
    block->lock_known = !block->lock_state[0];
    block->circuit_count = (unsigned)count;
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        const cJSON *item = cJSON_GetArrayItem(items, (int)i);
        lx_light_circuit_t *circuit = &block->circuits[i];
        light_copy(circuit->id, sizeof(circuit->id), light_string(item, "id"));
        light_copy(circuit->action, sizeof(circuit->action), light_string(item, "action"));
        light_copy(circuit->name, sizeof(circuit->name), light_string(item, "name"));
        light_copy(circuit->state, sizeof(circuit->state), light_string(item, "state"));
        light_copy(circuit->lock_state, sizeof(circuit->lock_state), light_string(item, "lock"));
        circuit->type = (lx_light_type_t)cJSON_GetObjectItemCaseSensitive(item, "type")->valueint;
        circuit->min_kelvin = (unsigned)cJSON_GetObjectItemCaseSensitive(item, "min")->valueint;
        circuit->max_kelvin = (unsigned)cJSON_GetObjectItemCaseSensitive(item, "max")->valueint;
        circuit->rgb = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "rgb"));
        circuit->white = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "white"));
        circuit->blocked = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "blocked"));
        circuit->lock_known = !circuit->lock_state[0];
        circuit->actual.saturation = 100;
        circuit->actual.kelvin = (circuit->min_kelvin + circuit->max_kelvin) / 2;
    }
    cJSON_Delete(meta);
    return block;
}

bool lx_light_block_minimize(lx_light_block_t *block)
{
    if (!block)
        return false;
    if (!block->circuits)
        return true;
    lx_light_observation_t *small =
        block->circuit_count ? lx_light_alloc(block->circuit_count, sizeof(*small)) : NULL;
    if (block->circuit_count && !small)
        return false;
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        const lx_light_circuit_t *circuit = &block->circuits[i];
        light_copy(small[i].state, sizeof(small[i].state), circuit->state);
        small[i].type = circuit->type;
        small[i].valid = circuit->actual.valid;
        small[i].on = circuit->actual.brightness > 0;
    }
    free(block->circuits);
    block->circuits = NULL;
    block->observations = small;
    return true;
}

void lx_light_block_free(lx_light_block_t *block)
{
    if (!block)
        return;
    free(block->moods);
    free(block->circuits);
    free(block->observations);
    free(block);
}

void lx_light_block_cancel(lx_light_block_t *block)
{
    if (!block || !block->circuits)
        return;
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        block->circuits[i].dirty = block->circuits[i].pending = false;
        ++block->circuits[i].serial;
    }
}

bool lx_light_color_parse(const char *text, size_t length, lx_light_value_t *value)
{
    if (!text || !value || !length || length > 80 || memchr(text, 0, length))
        return false;
    char input[81];
    memcpy(input, text, length);
    input[length] = 0;
    double a, b, d;
    int end = 0;
    lx_light_value_t next = *value;
    if (sscanf(input, "hsv( %lf , %lf , %lf ) %n", &a, &b, &d, &end) == 3 && end == (int)length)
    {
        if (!isfinite(a) || !isfinite(b) || !isfinite(d) || a < 0 || a > 360 || b < 0 || b > 100 ||
            d < 0 || d > 100)
            return false;
        next.mode = LX_LIGHT_MODE_HSV;
        next.hue = (unsigned)lround(a) % 360;
        next.saturation = (unsigned)lround(b);
        next.brightness = (unsigned)lround(d);
    }
    else if (sscanf(input, "temp( %lf , %lf ) %n", &a, &b, &end) == 2 && end == (int)length)
    {
        if (!isfinite(a) || !isfinite(b) || a < 0 || a > 100 || b < 1000 || b > 40000)
            return false;
        next.mode = LX_LIGHT_MODE_WHITE;
        next.brightness = (unsigned)lround(a);
        next.kelvin = (unsigned)lround(b);
    }
    else
        return false;
    next.valid = true;
    *value = next;
    return true;
}

static bool light_equal(const lx_light_value_t *a, const lx_light_value_t *b)
{
    return a->valid && b->valid && a->brightness == b->brightness && a->mode == b->mode &&
           (a->mode != LX_LIGHT_MODE_HSV || (a->hue == b->hue && a->saturation == b->saturation)) &&
           (a->mode != LX_LIGHT_MODE_WHITE || a->kelvin == b->kelvin);
}

static bool light_lock(const char *text, size_t length, bool *known)
{
    if (!length)
    {
        *known = true;
        return false;
    }
    cJSON *json = light_json(text, length);
    const cJSON *locked = cJSON_GetObjectItemCaseSensitive(json, "locked");
    *known = cJSON_IsNumber(locked) && locked->valuedouble >= 0 && locked->valuedouble <= 2;
    bool result = !*known || locked->valuedouble != 0;
    cJSON_Delete(json);
    return result;
}

void lx_light_block_event(lx_light_block_t *block,
                          const char *uuid,
                          bool numeric,
                          double value,
                          const char *text,
                          size_t length,
                          int64_t now)
{
    if (!block || !uuid || !uuid[0])
        return;
    if (!numeric && block->lock_state[0] && !strcmp(uuid, block->lock_state))
    {
        block->locked = light_lock(text, length, &block->lock_known);
        if (block->locked)
            lx_light_block_cancel(block);
    }
    if (!numeric && block->active_state[0] && !strcmp(uuid, block->active_state))
    {
        cJSON *list = light_json(text, length);
        int count = cJSON_GetArraySize(list);
        block->active_known = cJSON_IsArray(list) && count <= LX_LIGHT_MAX_MOODS;
        block->active_count = 0;
        const cJSON *id;
        cJSON_ArrayForEach(id, list)
        {
            if (!block->active_known)
                break;
            if (!cJSON_IsNumber(id) || !isfinite(id->valuedouble) || id->valuedouble < 0 ||
                id->valuedouble > INT_MAX || id->valuedouble != floor(id->valuedouble))
            {
                block->active_known = false;
                break;
            }
            unsigned mood = (unsigned)id->valuedouble;
            bool found = false;
            for (unsigned i = 0; i < block->active_count; ++i)
                if (block->active_ids[i] == mood)
                    found = true;
            if (mood && !found)
                block->active_ids[block->active_count++] = mood;
        }
        if (!block->active_known)
            block->active_count = 0;
        cJSON_Delete(list);
    }
    if (!numeric && block->moods_state[0] && !strcmp(uuid, block->moods_state))
    {
        cJSON *list = light_json(text, length);
        int count = cJSON_GetArraySize(list);
        lx_light_mood_t *moods = cJSON_IsArray(list) && count > 0 && count <= LX_LIGHT_MAX_MOODS
                                   ? lx_light_alloc((unsigned)count, sizeof(*moods))
                                   : NULL;
        bool ok =
            cJSON_IsArray(list) && count >= 0 && count <= LX_LIGHT_MAX_MOODS && (!count || moods);
        for (int i = 0; ok && i < count; ++i)
        {
            const cJSON *item = cJSON_GetArrayItem(list, i);
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
            ok = cJSON_IsNumber(id) && isfinite(id->valuedouble) && id->valuedouble >= 0 &&
                 id->valuedouble <= INT_MAX && id->valuedouble == floor(id->valuedouble);
            if (ok)
            {
                moods[i].id = (unsigned)id->valuedouble;
                light_copy(moods[i].name, sizeof(moods[i].name), light_string(item, "name"));
            }
        }
        /* Hibás friss névlista esetén nem maradhat érvényesnek látszó régi név. */
        free(block->moods);
        block->moods = ok ? moods : NULL;
        block->mood_count = ok ? (unsigned)count : 0;
        if (!ok)
            free(moods);
        cJSON_Delete(list);
    }
    if (!numeric && block->names_state[0] && !strcmp(uuid, block->names_state))
    {
        cJSON *names = light_json(text, length);
        if (block->circuits && cJSON_IsObject(names))
            for (unsigned i = 0; i < block->circuit_count; ++i)
            {
                lx_light_circuit_t *circuit = &block->circuits[i];
                const char *name = light_string(names, circuit->id);
                if (!name[0])
                    name = light_string(names, circuit->action);
                const char *suffix = strrchr(circuit->action, '/');
                if (!name[0] && suffix)
                    name = light_string(names, suffix + 1);
                if (name[0])
                    light_copy(circuit->name, sizeof(circuit->name), name);
            }
        cJSON_Delete(names);
    }
    if (block->observations)
    {
        for (unsigned i = 0; i < block->circuit_count; ++i)
        {
            lx_light_observation_t *item = &block->observations[i];
            if (!item->state[0] || strcmp(uuid, item->state))
                continue;
            if (numeric && (item->type == LX_LIGHT_SWITCH || item->type == LX_LIGHT_DIMMER))
            {
                item->valid = isfinite(value) && value >= 0 && value <= 100 &&
                              (item->type != LX_LIGHT_SWITCH || value == 0 || value == 1);
                item->on = item->valid && value > 0;
            }
            else if (!numeric && item->type == LX_LIGHT_COLOR)
            {
                lx_light_value_t color = {0};
                item->valid = lx_light_color_parse(text, length, &color);
                item->on = item->valid && color.brightness > 0;
            }
        }
        return;
    }
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        lx_light_circuit_t *circuit = &block->circuits[i];
        if (!numeric && circuit->lock_state[0] && !strcmp(uuid, circuit->lock_state))
        {
            circuit->locked = light_lock(text, length, &circuit->lock_known);
            if (circuit->locked)
                circuit->dirty = circuit->pending = false;
        }
        if (!circuit->state[0] || strcmp(uuid, circuit->state))
            continue;
        if (numeric && (circuit->type == LX_LIGHT_SWITCH || circuit->type == LX_LIGHT_DIMMER))
        {
            circuit->actual.valid = isfinite(value) && value >= 0 && value <= 100 &&
                                    (circuit->type != LX_LIGHT_SWITCH || value == 0 || value == 1);
            if (circuit->actual.valid)
                circuit->actual.brightness =
                    circuit->type == LX_LIGHT_SWITCH ? (value ? 100 : 0) : (unsigned)lround(value);
        }
        else if (!numeric && circuit->type == LX_LIGHT_COLOR)
            circuit->actual.valid = lx_light_color_parse(text, length, &circuit->actual);
        if (!circuit->dirty && circuit->pending &&
            (light_equal(&circuit->actual, &circuit->desired) || now >= circuit->pending_until_us))
            circuit->pending = false;
    }
}

static bool light_allowed(const lx_light_block_t *block)
{
    return block && !block->blocked && block->lock_known && !block->locked &&
           lx_light_action_valid(block->action);
}

void lx_light_summary(const lx_light_block_t *block, lx_light_summary_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!block)
        return;
    out->available = true;
    out->controllable = light_allowed(block);
    out->circuit_count = block->circuit_count;
    bool all_known = block->circuit_count > 0, any_on = false;
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        if (block->observations)
        {
            if (!block->observations[i].valid)
                all_known = false;
            else if (block->observations[i].on)
                any_on = true;
        }
        else
        {
            const lx_light_value_t *value = &block->circuits[i].actual;
            if (!value->valid)
                all_known = false;
            else if (value->brightness)
                any_on = true;
        }
    }
    if (all_known && !any_on)
    {
        out->label = LX_LIGHT_OFF;
        return;
    }
    if (block->active_known && block->active_count > 1)
    {
        out->label = LX_LIGHT_MANY_MOODS;
        out->active_count = block->active_count;
        return;
    }
    if (block->active_known && block->active_count == 1)
        for (unsigned i = 0; i < block->mood_count; ++i)
            if (block->moods[i].id == block->active_ids[0] && block->moods[i].name[0])
            {
                out->label = LX_LIGHT_ONE_MOOD;
                light_copy(out->name, sizeof(out->name), block->moods[i].name);
                return;
            }
    if (any_on)
        out->label = LX_LIGHT_CUSTOM;
}

bool lx_light_command_build(const lx_light_block_t *block,
                            unsigned index,
                            const lx_light_value_t *value,
                            char out[LX_LIGHT_COMMAND_SIZE])
{
    if (!light_allowed(block) || !block->circuits || index >= block->circuit_count || !value ||
        !out || !value->valid || value->brightness > 100)
        return false;
    const lx_light_circuit_t *circuit = &block->circuits[index];
    if (circuit->blocked || !circuit->lock_known || circuit->locked || !circuit->actual.valid ||
        !lx_light_action_valid(circuit->action))
        return false;
    char command[64];
    if (circuit->type == LX_LIGHT_SWITCH)
    {
        if (value->brightness != 0 && value->brightness != 100)
            return false;
        snprintf(command, sizeof(command), "%s", value->brightness ? "On" : "Off");
    }
    else if (circuit->type == LX_LIGHT_DIMMER)
        snprintf(command, sizeof(command), "%u", value->brightness);
    else if (circuit->type == LX_LIGHT_COLOR && value->mode == LX_LIGHT_MODE_HSV && circuit->rgb &&
             value->hue < 360 && value->saturation <= 100)
        snprintf(command,
                 sizeof(command),
                 "hsv(%u,%u,%u)",
                 value->hue,
                 value->saturation,
                 value->brightness);
    else if (circuit->type == LX_LIGHT_COLOR && value->mode == LX_LIGHT_MODE_WHITE &&
             circuit->white && value->kelvin >= circuit->min_kelvin &&
             value->kelvin <= circuit->max_kelvin)
        snprintf(command, sizeof(command), "temp(%u,%u)", value->brightness, value->kelvin);
    else
        return false;
    int count = snprintf(out, LX_LIGHT_COMMAND_SIZE, "jdev/sps/io/%s/%s", circuit->action, command);
    return count > 0 && count < LX_LIGHT_COMMAND_SIZE;
}

bool lx_light_queue(lx_light_block_t *block,
                    unsigned index,
                    const lx_light_value_t *value,
                    int64_t now)
{
    char command[LX_LIGHT_COMMAND_SIZE];
    if (now < 0 || !lx_light_command_build(block, index, value, command))
        return false;
    lx_light_circuit_t *circuit = &block->circuits[index];
    if ((circuit->pending && light_equal(&circuit->desired, value)) ||
        (!circuit->pending && light_equal(&circuit->actual, value)))
        return true;
    circuit->desired = *value;
    circuit->dirty = circuit->pending = true;
    circuit->pending_until_us = now + LX_LIGHT_CONFIRM_US;
    ++circuit->serial;
    return true;
}

bool lx_light_take(lx_light_block_t *block,
                   unsigned index,
                   int64_t now,
                   char out[LX_LIGHT_COMMAND_SIZE])
{
    if (!block || !block->circuits || index >= block->circuit_count || now < 0)
        return false;
    lx_light_circuit_t *circuit = &block->circuits[index];
    if (!circuit->dirty ||
        (circuit->last_sent_us && now - circuit->last_sent_us < LX_LIGHT_INTERVAL_US))
        return false;
    if (!lx_light_command_build(block, index, &circuit->desired, out))
    {
        circuit->dirty = circuit->pending = false;
        return false;
    }
    circuit->dirty = false;
    circuit->last_sent_us = now ? now : 1;
    circuit->pending_until_us = now + LX_LIGHT_CONFIRM_US;
    return true;
}

void lx_light_circuit_snapshot(const lx_light_block_t *block,
                               unsigned index,
                               int64_t now,
                               lx_light_circuit_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!block || !block->circuits || index >= block->circuit_count)
        return;
    *out = block->circuits[index];
    out->blocked = out->blocked || !light_allowed(block) || !out->lock_known || out->locked;
    if (out->pending && (out->dirty || now < out->pending_until_us))
        out->actual = out->desired;
    else
        out->pending = false;
}
