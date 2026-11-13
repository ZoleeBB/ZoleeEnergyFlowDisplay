/* A hét új blokk közös, platformfüggetlen állapotmodellje. A hálózati cím,
 * típus, korlát és zár mindig a normalizált struktúrából származik. */
#include "app_loxone_controls.h"

static const char *const controls_types[] = {
    "Dimmer", "CentralLightController", "Slider", "TimedSwitch", "WindowMonitor", "Gate", "Window"
};
static const char *const controls_names[] = {
    "dimmer", "central-lighting", "slider", "timed-switch", "window-monitor", "gate", "window"
};
static const char *const controls_states[LX_CONTROLS_STATE_COUNT] = {
    "value", "min", "max", "step", "error", "motion", "preventOpen", "preventClose",
    "deactivationDelay", "deactivationDelayTotal", "windowStates", "numOpen", "numClosed",
    "numTilted", "numOffline", "numLocked", "numUnlocked", "jLocked", "locked", "deviceState",
    "lockedReason", "targetPosition", "type"
};

static int controls_type_index(const char *type)
{
    for (unsigned i = 0; i < sizeof(controls_types) / sizeof(controls_types[0]); ++i)
        if (!strcmp(type, controls_types[i]))
            return (int)i;
    return -1;
}

const char *lx_controls_gate_icon(int animation)
{
    static const char *const icons[] = {
        "gate-garage", "gate-left", "gate-right", "gate-double", "gate-slide-left", "gate-slide-right"
    };
    return animation >= 0 && animation < 6 ? icons[animation] : "gate";
}

static bool controls_number(const cJSON *object, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble))
        return false;
    *out = item->valuedouble;
    return true;
}

static const cJSON *controls_reference(const cJSON *controls, const char *uuid, unsigned depth)
{
    if (!uuid[0] || depth > 8)
        return NULL;
    const cJSON *control;
    cJSON_ArrayForEach(control, controls)
    {
        if ((control->string && !strcmp(control->string, uuid)) ||
            !strcmp(light_string(control, "uuidAction"), uuid))
            return control;
        const cJSON *found = controls_reference(cJSON_GetObjectItemCaseSensitive(control, "subControls"),
                                                uuid, depth + 1);
        if (found)
            return found;
    }
    return NULL;
}

bool lx_controls_catalog_attach(cJSON *entry, const cJSON *control, bool parent_secured,
                                 const cJSON *rooms, const cJSON *structure)
{
    int index = controls_type_index(light_string(control, "type"));
    if (index < 0)
        return true;
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(control, "states");
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(control, "details");
    const char *action = control_action_uuid(control);
    bool blocked = light_blocked(control, parent_secured) || !lx_uuid_valid(action) ||
                   (states && !cJSON_IsObject(states));
    cJSON *meta = cJSON_CreateObject(), *state_meta = cJSON_CreateObject();
    if (!meta || !state_meta || !cJSON_AddItemToObject(meta, "states", state_meta))
    {
        cJSON_Delete(meta);
        cJSON_Delete(state_meta);
        return false;
    }
    bool ok = cJSON_AddStringToObject(meta, "action", action) &&
              cJSON_AddStringToObject(meta, "kind", controls_names[index]);
    for (unsigned i = 0; ok && i < LX_CONTROLS_STATE_COUNT; ++i)
    {
        const char *source = controls_states[i];
        if (i == LX_CONTROL_VALUE && (index == 0 || index == 5 || index == 6))
            source = "position";
        if (i == LX_CONTROL_MOTION)
            source = index == 5 ? "active" : "direction";
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(states, source);
        if (state && (!cJSON_IsString(state) || !lx_uuid_valid(state->valuestring)))
            blocked = true;
        ok = light_state_add(state_meta, controls_states[i], states, source);
    }
    double minimum = 0, maximum = 100, step = 1, animation = -1;
    bool range = index != 2 ||
        (controls_number(details, "min", &minimum) && controls_number(details, "max", &maximum) &&
         controls_number(details, "step", &step));
    range = range && minimum < maximum && minimum >= -1000000 && maximum <= 1000000 &&
            step > 0 && step <= maximum - minimum;
    (void)controls_number(details, "animation", &animation);
    if (animation < 0 || animation > 5 || animation != floor(animation))
        animation = -1;
    const cJSON *stairway = cJSON_GetObjectItemCaseSensitive(details, "isStairwayLs");
    if (index == 3 && !cJSON_IsBool(stairway))
        blocked = true;
    ok = ok && cJSON_AddBoolToObject(meta, "blocked", blocked) &&
         cJSON_AddBoolToObject(meta, "stairway", cJSON_IsTrue(stairway)) &&
         cJSON_AddBoolToObject(meta, "range", range) &&
         cJSON_AddNumberToObject(meta, "min", minimum) &&
         cJSON_AddNumberToObject(meta, "max", maximum) &&
         cJSON_AddNumberToObject(meta, "step", step) &&
         cJSON_AddNumberToObject(meta, "animation", animation) &&
         cJSON_AddStringToObject(meta, "format", light_string(details, "format"));
    if (ok && index == 4)
    {
        const cJSON *windows = cJSON_GetObjectItemCaseSensitive(details, "windows");
        cJSON *list = cJSON_CreateArray();
        ok = list && cJSON_AddItemToObject(meta, "windows", list);
        if (!ok)
            cJSON_Delete(list);
        unsigned count = 0;
        const cJSON *window;
        cJSON_ArrayForEach(window, windows)
        {
            if (!ok || count == LX_CONTROLS_MAX_WINDOWS)
                break;
            /* A gyártó indexe szerinti sor marad; a nested monitor nem lesz
             * rekurzívan összeadva, és a számlálókat sem a sorokból becsüljük. */
            cJSON *row = cJSON_CreateObject();
            const cJSON *reference = controls_reference(cJSON_GetObjectItemCaseSensitive(structure, "controls"),
                                                          light_string(window, "uuid"), 0);
            const char *room = light_string(window, "room");
            if (!room[0])
                room = light_string(reference, "room");
            const char *name = light_string(window, "name");
            if (!name[0])
                name = light_string(reference, "name");
            char row_name[LX_HOME_NAME_SIZE], row_room[65], row_place[65];
            light_copy(row_name, sizeof(row_name), name);
            light_copy(row_room, sizeof(row_room), light_string(cJSON_GetObjectItemCaseSensitive(rooms, room), "name"));
            light_copy(row_place, sizeof(row_place), light_string(window, "installPlace"));
            ok = row && cJSON_AddStringToObject(row, "name", row_name) &&
                 cJSON_AddStringToObject(row, "room", row_room) &&
                 cJSON_AddStringToObject(row, "place", row_place);
            if (!ok || !cJSON_AddItemToArray(list, row))
            {
                cJSON_Delete(row);
                ok = false;
                break;
            }
            ++count;
        }
        ok = ok && cJSON_AddBoolToObject(meta, "truncated",
                     cJSON_GetArraySize(windows) > (int)LX_CONTROLS_MAX_WINDOWS);
    }
    char *encoded = ok ? cJSON_PrintUnformatted(meta) : NULL;
    ok = encoded && strlen(encoded) <= LX_CONTROLS_META_MAX &&
         cJSON_AddStringToObject(entry, "controls_type", controls_names[index]) &&
         cJSON_AddBoolToObject(entry, "controls_blocked", blocked) &&
         cJSON_AddStringToObject(entry, "controls", encoded);
    if (ok && index == 5)
        ok = cJSON_AddNumberToObject(entry, "gate_animation", animation) != NULL;
    free(encoded);
    cJSON_Delete(meta);
    return ok;
}

const cJSON *lx_controls_catalog_find(const cJSON *catalog, const char *control_uuid,
                                      lx_home_card_type_t type)
{
    if (!cJSON_IsArray(catalog) || !lx_uuid_valid(control_uuid) || !lx_home_card_is_controls(type))
        return NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, catalog)
        if (!strcmp(light_string(entry, "control_uuid"), control_uuid) &&
            !strcmp(light_string(entry, "controls_type"), controls_names[type - LX_HOME_CARD_DIMMER]) &&
            !strcmp(light_string(entry, "type"), controls_types[type - LX_HOME_CARD_DIMMER]) &&
            cJSON_IsString(cJSON_GetObjectItemCaseSensitive(entry, "controls")))
            return entry;
    return NULL;
}

/* Csak egy számhely támogatott. A struktúrából kapott formátum soha nem
 * kerül printf formátumargumentumába, így %n és szélesség sem futtatható. */
static void controls_format(lx_controls_block_t *block, const char *format)
{
    block->decimals = block->type == LX_HOME_CARD_SLIDER ? 1 : 0;
    if (block->type == LX_HOME_CARD_DIMMER || block->type == LX_HOME_CARD_WINDOW ||
        block->type == LX_HOME_CARD_GATE)
        strcpy(block->unit, "%");
    if (!format || !format[0])
        return;
    const char *mark = strchr(format, '%');
    if (!mark || (size_t)(mark - format) >= sizeof(block->prefix))
        return;
    const char *end = mark + 1;
    unsigned decimals = 0;
    if (*end == '.')
    {
        ++end;
        if (*end < '0' || *end > '6')
            return;
        decimals = (unsigned)(*end++ - '0');
    }
    if (*end != 'f' && *end != 'd' && *end != 'i')
        return;
    ++end;
    size_t prefix = (size_t)(mark - format);
    memcpy(block->prefix, format, prefix);
    block->prefix[prefix] = 0;
    block->decimals = decimals;
    while (*end == ' ')
        ++end;
    char unit[25];
    unsigned pos = 0;
    while (*end && pos + 1 < sizeof(unit))
    {
        if (*end == '%')
        {
            if (end[1] != '%')
                return;
            ++end;
        }
        unit[pos++] = *end++;
    }
    unit[pos] = 0;
    light_copy(block->unit, sizeof(block->unit), unit);
}

lx_controls_block_t *lx_controls_block_create(const cJSON *entry)
{
    int index = controls_type_index(light_string(entry, "type"));
    if (index < 0 || strcmp(light_string(entry, "controls_type"), controls_names[index]))
        return NULL;
    const char *encoded = light_string(entry, "controls");
    cJSON *meta = strlen(encoded) <= LX_CONTROLS_META_MAX ? light_json_limit(encoded, strlen(encoded), LX_CONTROLS_META_MAX) : NULL;
    if (!cJSON_IsObject(meta) || strcmp(light_string(meta, "kind"), controls_names[index]))
    {
        cJSON_Delete(meta);
        return NULL;
    }
    lx_controls_block_t *block = lx_light_alloc(1, sizeof(*block));
    if (!block)
    {
        cJSON_Delete(meta);
        return NULL;
    }
    block->type = (lx_home_card_type_t)(LX_HOME_CARD_DIMMER + index);
    const char *action = light_string(meta, "action");
    if (lx_uuid_valid(action))
        light_copy(block->action, sizeof(block->action), action);
    block->blocked = !block->action[0] ||
        !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(meta, "blocked")) ||
        !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "controls_blocked"));
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(meta, "states");
    for (unsigned i = 0; i < LX_CONTROLS_STATE_COUNT; ++i)
    {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(states, controls_states[i]);
        const char *state = cJSON_IsString(item) ? item->valuestring : "";
        if (!cJSON_IsString(item) || (state[0] && !lx_uuid_valid(state)))
            block->blocked = true;
        else
            light_copy(block->states[i], LX_UUID_LEN, state);
    }
    block->lock_known = !block->states[LX_CONTROL_LOCK][0];
    block->stairway = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(meta, "stairway"));
    double animation;
    block->animation = controls_number(meta, "animation", &animation) &&
        animation >= 0 && animation <= 5 && animation == floor(animation) ? (int)animation : -1;
    if (block->type == LX_HOME_CARD_SLIDER && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(meta, "range")))
    {
        static const char *const keys[] = {"min", "max", "step"};
        for (unsigned i = 0; i < 3; ++i)
            if (controls_number(meta, keys[i], &block->values[LX_CONTROL_MIN + i]))
                block->known |= 1U << (LX_CONTROL_MIN + i);
    }
    controls_format(block, light_string(meta, "format"));
    if (block->type == LX_HOME_CARD_WINDOW_MONITOR)
    {
        const cJSON *list = cJSON_GetObjectItemCaseSensitive(meta, "windows");
        unsigned count = (unsigned)cJSON_GetArraySize(list);
        if (count <= LX_CONTROLS_MAX_WINDOWS && (!count ||
            (block->windows = lx_light_alloc(count, sizeof(*block->windows)))))
        {
            block->window_count = count;
            for (unsigned i = 0; i < count; ++i)
            {
                const cJSON *row = cJSON_GetArrayItem(list, (int)i);
                light_copy(block->windows[i].name, sizeof(block->windows[i].name), light_string(row, "name"));
                light_copy(block->windows[i].room, sizeof(block->windows[i].room), light_string(row, "room"));
                light_copy(block->windows[i].place, sizeof(block->windows[i].place), light_string(row, "place"));
            }
        }
        else
            block->list_truncated = true;
        block->list_truncated |= cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(meta, "truncated"));
    }
    cJSON_Delete(meta);
    return block;
}

void lx_controls_block_free(lx_controls_block_t *block)
{
    if (block)
        free(block->windows);
    free(block);
}

static void controls_window_states(lx_controls_block_t *block, const char *text, size_t length)
{
    block->list_valid = false;
    for (unsigned i = 0; i < block->window_count; ++i)
        block->windows[i].valid = false;
    if (!length && !block->window_count)
    {
        block->list_valid = true;
        return;
    }
    if (!text || !length || length > LX_LIGHT_TEXT_MAX)
        return;
    size_t pos = 0;
    unsigned index = 0;
    while (pos < length)
    {
        unsigned value = 0, digits = 0;
        while (pos < length && text[pos] >= '0' && text[pos] <= '9')
        {
            value = value * 10 + (unsigned)(text[pos++] - '0');
            if (++digits > 2 || value > 31)
                return;
        }
        if (!digits || (pos < length && text[pos] != ',') ||
            (pos < length && pos + 1 == length))
            return;
        if (index < block->window_count)
            block->windows[index].state = value;
        ++index;
        if (pos < length)
            ++pos;
    }
    if (index < block->window_count || (!block->list_truncated && index != block->window_count))
        return;
    block->list_valid = true;
    for (unsigned i = 0; i < block->window_count; ++i)
        block->windows[i].valid = true;
}

void lx_controls_block_event(lx_controls_block_t *block, const char *uuid, bool numeric,
                              double value, const char *text, size_t length, int64_t received_us)
{
    if (!block || !uuid || !uuid[0])
        return;
    for (unsigned i = 0; i < LX_CONTROLS_STATE_COUNT; ++i)
    {
        if (!block->states[i][0] || strcmp(block->states[i], uuid))
            continue;
        block->known &= ~(1U << i);
        if (i == LX_CONTROL_LOCK)
        {
            block->lock_known = false;
            block->locked = numeric || light_lock(text, length, &block->lock_known);
        }
        else if (i == LX_CONTROL_REASON)
        {
            block->reason[0] = 0;
            if (!numeric && text && length < sizeof(block->reason) && !memchr(text, 0, length))
            {
                memcpy(block->reason, text, length);
                block->reason[length] = 0;
            }
        }
        else if (i == LX_CONTROL_WINDOWS)
            controls_window_states(block, numeric ? NULL : text, numeric ? 0 : length);
        else if (numeric && isfinite(value))
        {
            bool valid = true;
            if (i == LX_CONTROL_ERROR || i == LX_CONTROL_PREVENT_OPEN || i == LX_CONTROL_PREVENT_CLOSE ||
                i == LX_CONTROL_HARD_LOCK)
                valid = value == 0 || value == 1;
            else if (i == LX_CONTROL_MOTION)
                valid = value == -1 || value == 0 || value == 1;
            else if (i == LX_CONTROL_DEVICE)
                valid = value == 0 || value == 1 || value == 2;
            else if (i >= LX_CONTROL_NUM_OPEN && i <= LX_CONTROL_NUM_UNLOCKED)
                valid = value >= 0 && value <= 1000000 && value == floor(value);
            else if (i == LX_CONTROL_DELAY)
                valid = value == -1 || (value >= 0 && value <= 31536000);
            else if (i == LX_CONTROL_TOTAL)
                valid = value >= 0 && value <= 31536000;
            if (valid)
            {
                block->known |= 1U << i;
                block->values[i] = value;
                if (i == LX_CONTROL_DELAY)
                    block->delay_received_us = received_us;
            }
        }
    }
}

static bool controls_known(const lx_controls_block_t *block, unsigned state)
{
    return (block->known & (1U << state)) != 0;
}

static bool controls_optional_zero(const lx_controls_block_t *block, unsigned state)
{
    return !block->states[state][0] ||
           (controls_known(block, state) && block->values[state] == 0);
}

void lx_controls_summary(const lx_controls_block_t *block, int64_t now,
                          lx_controls_summary_t *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->animation = -1;
    if (!block)
        return;
    out->type = block->type;
    out->available = true;
    out->animation = block->animation;
    out->decimals = block->decimals;
    light_copy(out->unit, sizeof(out->unit), block->unit);
    light_copy(out->prefix, sizeof(out->prefix), block->prefix);
    light_copy(out->reason, sizeof(out->reason), block->reason);
    out->controllable = !block->blocked && block->lock_known && !block->locked &&
        controls_optional_zero(block, LX_CONTROL_HARD_LOCK) &&
        (!block->states[LX_CONTROL_DEVICE][0] ||
         (controls_known(block, LX_CONTROL_DEVICE) && block->values[LX_CONTROL_DEVICE] != 1)) &&
        controls_optional_zero(block, LX_CONTROL_ERROR) && block->type != LX_HOME_CARD_WINDOW_MONITOR;
    out->open_allowed = out->controllable && controls_optional_zero(block, LX_CONTROL_PREVENT_OPEN);
    out->close_allowed = out->controllable && controls_optional_zero(block, LX_CONTROL_PREVENT_CLOSE);
    out->value = block->values[LX_CONTROL_VALUE];
    out->value_valid = controls_known(block, LX_CONTROL_VALUE);
    out->minimum = block->values[LX_CONTROL_MIN];
    out->maximum = block->values[LX_CONTROL_MAX];
    out->step = block->values[LX_CONTROL_STEP];
    out->range_valid = controls_known(block, LX_CONTROL_MIN) && controls_known(block, LX_CONTROL_MAX) &&
        controls_known(block, LX_CONTROL_STEP) && out->minimum >= -1000000 && out->maximum <= 1000000 &&
        out->minimum < out->maximum && out->step > 0 && out->step <= out->maximum - out->minimum;
    if (block->type == LX_HOME_CARD_WINDOW || block->type == LX_HOME_CARD_GATE)
    {
        out->value_valid = out->value_valid && out->value >= 0 && out->value <= 1;
        out->value *= 100;
        out->minimum = 0;
        out->maximum = 100;
        out->step = 1;
        out->range_valid = true;
    }
    else if (block->type == LX_HOME_CARD_DIMMER || block->type == LX_HOME_CARD_SLIDER)
        out->value_valid = out->value_valid && out->range_valid &&
            ((out->value >= out->minimum && out->value <= out->maximum) ||
             (block->type == LX_HOME_CARD_DIMMER && out->value == 0));
    out->motion_valid = controls_known(block, LX_CONTROL_MOTION);
    out->motion = out->motion_valid ? (int)block->values[LX_CONTROL_MOTION] : 0;
    out->stairway = block->stairway;
    out->timer_valid = controls_known(block, LX_CONTROL_DELAY);
    out->timer_seconds = block->values[LX_CONTROL_DELAY];
    out->timer_running = out->timer_valid && out->timer_seconds > 0;
    if (out->timer_valid && out->timer_seconds > 0 && now >= block->delay_received_us)
    {
        out->timer_seconds -= (double)(now - block->delay_received_us) / 1000000;
        if (out->timer_seconds < 0)
            out->timer_seconds = 0;
        /* A nullára becsült kijelzés nem állapotválasz: a tényleges Ki-t
         * továbbra is a következő deactivationDelay esemény adja. */
    }
    out->timer_total_valid = controls_known(block, LX_CONTROL_TOTAL);
    out->timer_total = out->timer_total_valid ? block->values[LX_CONTROL_TOTAL] : 0;
    out->window_count = block->window_count;
    out->list_valid = block->list_valid;
    out->list_truncated = block->list_truncated;
    for (unsigned i = 0; i < LX_CONTROLS_COUNT_COUNT; ++i)
        if (controls_known(block, LX_CONTROL_NUM_OPEN + i))
        {
            out->counts_known |= 1U << i;
            out->counts[i] = (unsigned)block->values[LX_CONTROL_NUM_OPEN + i];
        }
    out->pending = block->dirty;
    out->failed = block->failed;
}

static bool controls_target(const lx_controls_summary_t *summary, double value, double *target)
{
    if (!summary->range_valid || !isfinite(value) || value < summary->minimum || value > summary->maximum)
        return false;
    double steps = round((value - summary->minimum) / summary->step);
    *target = summary->minimum + steps * summary->step;
    if (*target > summary->maximum)
        *target = summary->maximum;
    return isfinite(*target);
}

bool lx_controls_command(const lx_controls_block_t *block, lx_controls_action_t action,
                          double value, char out[96])
{
    if (!block || !out || !lx_uuid_valid(block->action))
        return false;
    lx_controls_summary_t summary;
    lx_controls_summary(block, block->delay_received_us, &summary);
    if (!summary.controllable)
        return false;
    const char *command = NULL;
    switch (block->type)
    {
    case LX_HOME_CARD_DIMMER:
        if (action == LX_CONTROLS_ON) command = "on";
        if (action == LX_CONTROLS_OFF) command = "off";
        break;
    case LX_HOME_CARD_CENTRAL_LIGHTING:
        if (action == LX_CONTROLS_ON) command = "on";
        if (action == LX_CONTROLS_OFF) command = "reset";
        break;
    case LX_HOME_CARD_TIMED_SWITCH:
        if (action == LX_CONTROLS_ON) command = "on";
        if (action == LX_CONTROLS_OFF) command = "off";
        if (action == LX_CONTROLS_PULSE) command = "pulse";
        break;
    case LX_HOME_CARD_GATE:
    case LX_HOME_CARD_WINDOW:
        if (action == LX_CONTROLS_OPEN && summary.open_allowed)
            command = block->type == LX_HOME_CARD_GATE ? "open" : "fullopen";
        if (action == LX_CONTROLS_CLOSE && summary.close_allowed)
            command = block->type == LX_HOME_CARD_GATE ? "close" : "fullclose";
        if (action == LX_CONTROLS_STOP) command = "stop";
        break;
    default:
        break;
    }
    if (command)
        return snprintf(out, 96, "jdev/sps/io/%s/%s", block->action, command) < 96;
    if (action != LX_CONTROLS_SET ||
        (block->type != LX_HOME_CARD_DIMMER && block->type != LX_HOME_CARD_SLIDER &&
         block->type != LX_HOME_CARD_WINDOW))
        return false;
    double target;
    if (!controls_target(&summary, value, &target))
        return false;
    if (block->type == LX_HOME_CARD_WINDOW)
    {
        /* A százalékos cél sem kerülheti meg az irányonkénti tiltást.
         * Ismeretlen pozíciónál csak két szabad irány mellett dönthető el,
         * hogy a kért mozgatás megengedett; egyenlő cél nem indít új irányt. */
        if ((!summary.open_allowed || !summary.close_allowed) && !summary.value_valid)
            return false;
        if ((target > summary.value && !summary.open_allowed) ||
            (target < summary.value && !summary.close_allowed))
            return false;
    }
    return snprintf(out, 96, "jdev/sps/io/%s/%s%.6f", block->action,
                     block->type == LX_HOME_CARD_WINDOW ? "moveToPosition/" : "", target) < 96;
}

bool lx_controls_queue(lx_controls_block_t *block, double value, int64_t now)
{
    char command[96];
    if (!block || block->type != LX_HOME_CARD_DIMMER || now < 0 ||
        !lx_controls_command(block, LX_CONTROLS_SET, value, command))
        return false;
    block->desired = value;
    block->dirty = true;
    block->failed = false;
    block->desired_until_us = now + 2000000;
    return true;
}

bool lx_controls_take(lx_controls_block_t *block, int64_t now, char out[96])
{
    if (!block || !block->dirty || now < block->next_send_us)
        return false;
    block->dirty = false;
    if (now < 0 || now > block->desired_until_us ||
        !lx_controls_command(block, LX_CONTROLS_SET, block->desired, out))
    {
        block->failed = true;
        return false;
    }
    block->next_send_us = now + 100000;
    return true;
}
