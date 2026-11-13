/* A közös Loxone-modell része, a világítási modell után. Annak korlátos
 * JSON-, jogosultság- és zároláskezelő segédeit használja; nincs új task. */
#include "app_loxone_irc.h"

static lx_irc_unit_t irc_format_unit(const cJSON *control)
{
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(control, "details");
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(details, "format");
    if (!format)
        return LX_IRC_UNIT_UNKNOWN;
    if (!cJSON_IsString(format))
        return LX_IRC_UNIT_INVALID;
    bool celsius = strstr(format->valuestring, "°C") != NULL;
    bool fahrenheit = strstr(format->valuestring, "°F") != NULL;
    if (celsius && fahrenheit)
        return LX_IRC_UNIT_INVALID;
    return celsius ? LX_IRC_UNIT_CELSIUS :
           fahrenheit ? LX_IRC_UNIT_FAHRENHEIT : LX_IRC_UNIT_UNKNOWN;
}

static bool irc_unit_valid(lx_irc_unit_t unit)
{
    return unit == LX_IRC_UNIT_CELSIUS || unit == LX_IRC_UNIT_FAHRENHEIT;
}

bool lx_irc_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured,
                           lx_irc_unit_t miniserver_unit)
{
    if (strcmp(light_string(control, "type"), "IRoomControllerV2"))
        return true;
    lx_irc_unit_t format_unit = irc_format_unit(control);
    lx_irc_unit_t unit = miniserver_unit == LX_IRC_UNIT_UNKNOWN ? format_unit : miniserver_unit;
    /* Ellentmondó vagy sérült egységből nem küldhető találgatott lépésköz. */
    if (format_unit == LX_IRC_UNIT_INVALID ||
        (irc_unit_valid(format_unit) && irc_unit_valid(unit) && format_unit != unit))
        unit = LX_IRC_UNIT_INVALID;
    const char *action = control_action_uuid(control);
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(control, "states");
    const cJSON *lock = cJSON_GetObjectItemCaseSensitive(states, "jLocked");
    bool invalid_lock = lock && (!cJSON_IsString(lock) || !lx_uuid_valid(lock->valuestring));
    bool blocked = light_blocked(control, parent_secured) || !lx_uuid_valid(action) ||
                   !irc_unit_valid(unit) || invalid_lock ||
                   !lx_uuid_valid(light_string(states, "tempTarget"));
    cJSON *meta = cJSON_CreateObject();
    bool ok = meta && cJSON_AddStringToObject(meta, "action", action) &&
              cJSON_AddBoolToObject(meta, "blocked", blocked) &&
              cJSON_AddNumberToObject(meta, "unit", unit) &&
              light_state_add(meta, "target", states, "tempTarget") &&
              light_state_add(meta, "actual", states, "tempActual") &&
              light_state_add(meta, "current_mode", states, "currentMode") &&
              light_state_add(meta, "operating_mode", states, "operatingMode") &&
              light_state_add(meta, "lock", states, "jLocked");
    char *encoded = ok ? cJSON_PrintUnformatted(meta) : NULL;
    ok = encoded && strlen(encoded) <= LX_IRC_META_MAX &&
         cJSON_AddBoolToObject(entry, "irc_blocked", blocked) &&
         cJSON_AddNumberToObject(entry, "irc_unit", unit) &&
         cJSON_AddStringToObject(entry, "irc", encoded);
    free(encoded);
    cJSON_Delete(meta);
    return ok;
}

const cJSON *lx_irc_catalog_find(const cJSON *catalog, const char *control_uuid)
{
    if (!cJSON_IsArray(catalog) || !lx_uuid_valid(control_uuid))
        return NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, catalog)
        if (!strcmp(light_string(entry, "control_uuid"), control_uuid) &&
            !strcmp(light_string(entry, "type"), "IRoomControllerV2") &&
            cJSON_IsString(cJSON_GetObjectItemCaseSensitive(entry, "irc")))
            return entry;
    return NULL;
}

static bool irc_state_copy(char out[LX_UUID_LEN], const cJSON *meta, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(meta, key);
    if (!cJSON_IsString(item) || (item->valuestring[0] && !lx_uuid_valid(item->valuestring)))
        return false;
    light_copy(out, LX_UUID_LEN, item->valuestring);
    return true;
}

lx_irc_block_t *lx_irc_block_create(const cJSON *entry)
{
    if (strcmp(light_string(entry, "type"), "IRoomControllerV2"))
        return NULL;
    const char *encoded = light_string(entry, "irc");
    size_t length = strlen(encoded);
    cJSON *meta = length <= LX_IRC_META_MAX ? light_json(encoded, length) : NULL;
    if (!cJSON_IsObject(meta))
    {
        cJSON_Delete(meta);
        return NULL;
    }
    lx_irc_block_t *block = lx_light_alloc(1, sizeof(*block));
    bool ok = block && irc_state_copy(block->target_state, meta, "target") &&
              irc_state_copy(block->actual_state, meta, "actual") &&
              irc_state_copy(block->current_mode_state, meta, "current_mode") &&
              irc_state_copy(block->operating_mode_state, meta, "operating_mode") &&
              irc_state_copy(block->lock_state, meta, "lock");
    if (ok)
    {
        const cJSON *unit = cJSON_GetObjectItemCaseSensitive(meta, "unit");
        block->unit = cJSON_IsNumber(unit) && unit->valuedouble == 0 ? LX_IRC_UNIT_CELSIUS :
                      cJSON_IsNumber(unit) && unit->valuedouble == 1 ? LX_IRC_UNIT_FAHRENHEIT :
                      LX_IRC_UNIT_INVALID;
        const char *action = light_string(meta, "action");
        if (lx_uuid_valid(action))
            light_copy(block->action, sizeof(block->action), action);
        block->blocked = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(meta, "blocked")) ||
                         !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "irc_blocked")) ||
                         !block->action[0] || !irc_unit_valid(block->unit);
        block->lock_known = !block->lock_state[0];
    }
    cJSON_Delete(meta);
    if (!ok)
    {
        lx_irc_block_free(block);
        return NULL;
    }
    return block;
}

void lx_irc_block_free(lx_irc_block_t *block)
{
    free(block);
}

static bool irc_temperature(lx_irc_unit_t unit, double raw, double *celsius)
{
    if (!irc_unit_valid(unit) || !isfinite(raw))
        return false;
    double value = unit == LX_IRC_UNIT_FAHRENHEIT ? (raw - 32.0) / 1.8 : raw;
    /* A Miniserver hiányzó értéke (pl. -1000) nem jelenhet meg hőfokként.
     * A tág fizikai keret nem a felhasználó szabályozási tartományát állítja. */
    if (!isfinite(value) || value <= -273.15 || value > 1000.0)
        return false;
    *celsius = value;
    return true;
}

void lx_irc_block_event(lx_irc_block_t *block, const char *uuid, bool numeric,
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
    if (block->target_state[0] && !strcmp(uuid, block->target_state))
    {
        block->target_valid = numeric && irc_temperature(block->unit, value, &block->target_c);
        if (!block->target_valid)
            block->target_c = 0;
    }
    if (block->actual_state[0] && !strcmp(uuid, block->actual_state))
    {
        block->actual_valid = numeric && irc_temperature(block->unit, value, &block->actual_c);
        if (!block->actual_valid)
            block->actual_c = 0;
    }
    bool mode_valid = numeric && isfinite(value) && value >= -1 && value <= 5 && value == floor(value);
    if (block->current_mode_state[0] && !strcmp(uuid, block->current_mode_state))
    {
        block->current_mode_known = mode_valid;
        block->current_mode = mode_valid ? (int)value : -1;
    }
    if (block->operating_mode_state[0] && !strcmp(uuid, block->operating_mode_state))
    {
        block->operating_mode_known = mode_valid;
        block->operating_mode = mode_valid ? (int)value : -1;
    }
}

void lx_irc_summary(const lx_irc_block_t *block, lx_irc_summary_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!block)
        return;
    out->target_c = block->target_c;
    out->actual_c = block->actual_c;
    out->target_valid = block->target_valid;
    out->actual_valid = block->actual_valid;
    out->available = block->target_valid || block->actual_valid;
    out->controllable = block->target_valid && !block->blocked &&
                        block->lock_known && !block->locked;
    int mode = block->current_mode_known ? block->current_mode :
               block->operating_mode_known ? block->operating_mode : -1;
    if (mode == 1 || mode == 4)
        out->direction = LX_IRC_DIRECTION_HEATING;
    else if (mode == 2 || mode == 5)
        out->direction = LX_IRC_DIRECTION_COOLING;
}
