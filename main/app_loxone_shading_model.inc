/* A közös Loxone-modell része: az árnyékolás nem nyit külön kapcsolatot.
 * A korlátos metaadatot és a látható blokkok állapotát PSRAM-ban tartjuk. */
#include "app_loxone_shading.h"

static bool shading_optional_state_invalid(const cJSON *states, const char *name)
{
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(states, name);
    return state && (!cJSON_IsString(state) || !lx_uuid_valid(state->valuestring));
}

bool lx_shading_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured)
{
    const char *type = light_string(control, "type");
    bool central = !strcmp(type, "CentralJalousie");
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(control, "details");
    if (!central && (strcmp(type, "Jalousie") ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(details, "isAutomatic"))))
        return true;
    const char *action = control_action_uuid(control);
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(control, "states");
    /* Sérült zár-/eszközazonosító nem válhat hiányzó, tehát feloldott állapottá.
     * A pozíció és a mozgás csak kijelzés; hiányuk nem tiltja a három gombot. */
    bool blocked = light_blocked(control, parent_secured) || !lx_uuid_valid(action) ||
                   (states && !cJSON_IsObject(states)) ||
                   shading_optional_state_invalid(states, "locked") ||
                   shading_optional_state_invalid(states, "jLocked") ||
                   shading_optional_state_invalid(states, "deviceState");
    cJSON *meta = cJSON_CreateObject();
    bool ok = meta && cJSON_AddStringToObject(meta, "action", action) &&
              cJSON_AddBoolToObject(meta, "blocked", blocked) &&
              cJSON_AddBoolToObject(meta, "central", central) &&
              light_state_add(meta, "position", states, "position") &&
              light_state_add(meta, "up", states, "up") &&
              light_state_add(meta, "down", states, "down") &&
              light_state_add(meta, "locked", states, "locked") &&
              light_state_add(meta, "safety", states, "safetyActive") &&
              light_state_add(meta, "device", states, "deviceState") &&
              light_state_add(meta, "lock", states, "jLocked");
    char *encoded = ok ? cJSON_PrintUnformatted(meta) : NULL;
    ok = encoded && strlen(encoded) <= LX_SHADING_META_MAX &&
         cJSON_AddBoolToObject(entry, "shading_blocked", blocked) &&
         cJSON_AddBoolToObject(entry, "shading_central", central) &&
         cJSON_AddStringToObject(entry, "shading", encoded);
    free(encoded);
    cJSON_Delete(meta);
    return ok;
}

static bool shading_entry_type(const cJSON *entry, bool *central)
{
    const char *type = light_string(entry, "type");
    const cJSON *flag = cJSON_GetObjectItemCaseSensitive(entry, "shading_central");
    if (!strcmp(type, "CentralJalousie") && cJSON_IsTrue(flag))
    {
        *central = true;
        return true;
    }
    if (!strcmp(type, "Jalousie") && cJSON_IsFalse(flag))
    {
        *central = false;
        return true;
    }
    return false;
}

const cJSON *lx_shading_catalog_find(const cJSON *catalog, const char *control_uuid)
{
    if (!cJSON_IsArray(catalog) || !lx_uuid_valid(control_uuid))
        return NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, catalog)
    {
        bool central;
        if (!strcmp(light_string(entry, "control_uuid"), control_uuid) &&
            shading_entry_type(entry, &central) &&
            cJSON_IsString(cJSON_GetObjectItemCaseSensitive(entry, "shading")))
            return entry;
    }
    return NULL;
}

static bool shading_state_copy(char out[LX_UUID_LEN], const cJSON *meta, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(meta, key);
    if (!cJSON_IsString(item) || (item->valuestring[0] && !lx_uuid_valid(item->valuestring)))
        return false;
    light_copy(out, LX_UUID_LEN, item->valuestring);
    return true;
}

lx_shading_block_t *lx_shading_block_create(const cJSON *entry)
{
    bool central;
    if (!shading_entry_type(entry, &central))
        return NULL;
    const char *encoded = light_string(entry, "shading");
    size_t length = strlen(encoded);
    cJSON *meta = length <= LX_SHADING_META_MAX ? light_json(encoded, length) : NULL;
    const cJSON *meta_central = cJSON_GetObjectItemCaseSensitive(meta, "central");
    if (!cJSON_IsObject(meta) || !cJSON_IsBool(meta_central) ||
        cJSON_IsTrue(meta_central) != central)
    {
        cJSON_Delete(meta);
        return NULL;
    }
    lx_shading_block_t *block = lx_light_alloc(1, sizeof(*block));
    bool ok = block && shading_state_copy(block->position_state, meta, "position") &&
              shading_state_copy(block->up_state, meta, "up") &&
              shading_state_copy(block->down_state, meta, "down") &&
              shading_state_copy(block->locked_state, meta, "locked") &&
              shading_state_copy(block->safety_state, meta, "safety") &&
              shading_state_copy(block->device_state, meta, "device") &&
              shading_state_copy(block->lock_state, meta, "lock");
    if (ok)
    {
        const char *action = light_string(meta, "action");
        if (lx_uuid_valid(action))
            light_copy(block->action, sizeof(block->action), action);
        block->central = central;
        block->blocked = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(meta, "blocked")) ||
                         !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "shading_blocked")) ||
                         !block->action[0];
        block->hard_lock_known = !block->locked_state[0];
        block->lock_known = !block->lock_state[0];
        block->device_known = !block->device_state[0];
    }
    cJSON_Delete(meta);
    if (!ok)
    {
        lx_shading_block_free(block);
        return NULL;
    }
    return block;
}

void lx_shading_block_free(lx_shading_block_t *block)
{
    free(block);
}

void lx_shading_block_event(lx_shading_block_t *block, const char *uuid, bool numeric,
                            double value, const char *text, size_t length, int64_t now)
{
    (void)now;
    if (!block || !uuid || !uuid[0])
        return;
    if (block->lock_state[0] && !strcmp(uuid, block->lock_state))
    {
        block->lock_known = false;
        block->locked = numeric || light_lock(text, length, &block->lock_known);
    }
    bool binary = numeric && (value == 0 || value == 1);
    if (block->locked_state[0] && !strcmp(uuid, block->locked_state))
    {
        block->hard_lock_known = binary;
        block->hard_locked = !binary || value == 1;
    }
    if (block->device_state[0] && !strcmp(uuid, block->device_state))
    {
        block->device_known = numeric && (value == 0 || value == 1 || value == 2);
        block->device = block->device_known ? (int)value : 1;
    }
    if (block->safety_state[0] && !strcmp(uuid, block->safety_state))
        block->safety_active = binary && value == 1;
    /* A központi blokkhoz a gyártó nem definiál összesített pozíciót vagy
     * mozgásállapotot; csoporttagokból és elküldött parancsból sem becsüljük. */
    if (block->central)
        return;
    if (block->position_state[0] && !strcmp(uuid, block->position_state))
    {
        block->position_valid = numeric && isfinite(value) && value >= 0 && value <= 1;
        block->position_percent = block->position_valid ? value * 100.0 : 0;
    }
    if (block->up_state[0] && !strcmp(uuid, block->up_state))
    {
        block->up_known = binary;
        block->moving_up = binary && value == 1;
    }
    if (block->down_state[0] && !strcmp(uuid, block->down_state))
    {
        block->down_known = binary;
        block->moving_down = binary && value == 1;
    }
}

void lx_shading_summary(const lx_shading_block_t *block, lx_shading_summary_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!block)
        return;
    out->central = block->central;
    out->position_valid = !block->central && block->position_valid;
    out->position_percent = out->position_valid ? block->position_percent : 0;
    /* A szerkezetben szereplő, állapot nélküli központi blokk is használható.
     * Az élő hitelesített katalógust és kapcsolatot a szolgáltatás ellenőrzi. */
    out->available = block->action[0] && block->device_known && block->device != 1;
    out->controllable = out->available && !block->blocked &&
                        block->hard_lock_known && !block->hard_locked &&
                        block->lock_known && !block->locked;
    out->moving_up = !block->central && block->up_known && block->moving_up;
    out->moving_down = !block->central && block->down_known && block->moving_down;
    out->motion_known = !block->central && block->up_known && block->down_known &&
                        !(block->moving_up && block->moving_down);
    out->safety_active = block->safety_active;
}
