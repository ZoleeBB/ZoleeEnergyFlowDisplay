/* Az alkalmazásmodell része. A hálózattól független Audio-parser ugyanazokkal
 * a tesztekkel fut a gazdagépen és az ESP32-n. */
#include "app_loxone_audio.h"
#include <ctype.h>
#include <limits.h>

static const char *audio_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static bool audio_uint(const cJSON *value, unsigned *out)
{
    if (cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
        value->valuedouble >= 0 && value->valuedouble <= UINT_MAX &&
        floor(value->valuedouble) == value->valuedouble)
    {
        *out = (unsigned)value->valuedouble;
        return true;
    }
    if (cJSON_IsString(value) && value->valuestring && value->valuestring[0])
    {
        unsigned result = 0;
        for (const char *p = value->valuestring; *p; ++p)
        {
            if (*p < '0' || *p > '9' || result > (UINT_MAX - (unsigned)(*p - '0')) / 10U)
                return false;
            result = result * 10U + (unsigned)(*p - '0');
        }
        *out = result;
        return true;
    }
    return false;
}

static bool audio_host(const char *value, char out[LX_AUDIO_HOST_SIZE], unsigned *port)
{
    if (!value || !value[0] || strlen(value) >= LX_AUDIO_HOST_SIZE)
        return false;
    const char *colon = strrchr(value, ':');
    size_t length = colon ? (size_t)(colon - value) : strlen(value);
    /* A Miniserver által szolgáltatott helyi IPv4/DNS cím. URL, útvonal,
     * felhasználó és fejléctörés nem válhat hálózati célponttá. */
    if (!length)
        return false;
    for (size_t i = 0; i < length; ++i)
        if (!isalnum((unsigned char)value[i]) && value[i] != '.' && value[i] != '-')
            return false;
    *port = 7091;
    if (colon)
    {
        unsigned number = 0;
        if (!colon[1])
            return false;
        for (const char *p = colon + 1; *p; ++p)
        {
            if (*p < '0' || *p > '9' || number > 6553)
                return false;
            number = number * 10 + (unsigned)(*p - '0');
        }
        if (!number || number > 65535)
            return false;
        *port = number;
    }
    memcpy(out, value, length);
    out[length] = 0;
    return true;
}

static bool audio_state_meta(cJSON *meta, const cJSON *states, const char *name,
                              bool *blocked)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(states, name);
    if (value && (!cJSON_IsString(value) || !lx_uuid_valid(value->valuestring)))
        *blocked = true;
    return cJSON_AddStringToObject(meta, name,
        cJSON_IsString(value) && lx_uuid_valid(value->valuestring) ? value->valuestring : "") != NULL;
}

bool lx_audio_catalog_attach(cJSON *entry, const cJSON *control, bool secured,
                             const cJSON *structure)
{
    if (strcmp(audio_string(control, "type"), "AudioZoneV2"))
        return true;
    const cJSON *details = cJSON_GetObjectItemCaseSensitive(control, "details");
    const cJSON *servers = cJSON_GetObjectItemCaseSensitive(structure, "mediaServer");
    const cJSON *server = cJSON_GetObjectItemCaseSensitive(servers, audio_string(details, "server"));
    unsigned player = 0, type = 0, modes = 0, port = 7091;
    char host[LX_AUDIO_HOST_SIZE] = {0};
    const char *address = audio_string(server, "localIP");
    if (!address[0])
        address = audio_string(server, "host");
    bool valid = audio_uint(cJSON_GetObjectItemCaseSensitive(details, "playerid"), &player) && player &&
                 audio_uint(cJSON_GetObjectItemCaseSensitive(server, "type"), &type) && type == 2 &&
                 audio_host(address, host, &port);
    const cJSON *mode = cJSON_GetObjectItemCaseSensitive(details, "volumeModes");
    if (mode && (!audio_uint(mode, &modes) || modes > 7))
        valid = false;
    const char *action = control_action_uuid(control);
    bool blocked = !valid || !lx_uuid_valid(action) || light_blocked(control, secured) ||
        cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(details, "validOutputs"));
    cJSON *meta = cJSON_CreateObject();
    const cJSON *states = cJSON_GetObjectItemCaseSensitive(control, "states");
    const char *keys[] = {"isLocked", "jLocked", "volume", "maxVolume", "playState", "serverState", "clientState"};
    bool ok = meta && cJSON_AddStringToObject(meta, "host", host) &&
              cJSON_AddNumberToObject(meta, "port", port) &&
              cJSON_AddNumberToObject(meta, "player", player) &&
              cJSON_AddNumberToObject(meta, "volumeModes", modes) &&
              cJSON_AddStringToObject(meta, "action", action);
    for (unsigned i = 0; ok && i < sizeof(keys) / sizeof(keys[0]); ++i)
        ok = audio_state_meta(meta, states, keys[i], &blocked);
    cJSON *state_map = ok ? cJSON_CreateObject() : NULL;
    if (ok && state_map)
    {
        for (unsigned i = 0; ok && i < sizeof(keys) / sizeof(keys[0]); ++i)
            ok = cJSON_AddStringToObject(state_map, keys[i], audio_string(meta, keys[i])) != NULL;
        if (ok)
            ok = cJSON_AddItemToObject(meta, "states", state_map);
        if (!ok)
            cJSON_Delete(state_map);
    }
    else
        ok = false;
    ok = ok && cJSON_AddBoolToObject(meta, "blocked", blocked);
    char *encoded = ok ? cJSON_PrintUnformatted(meta) : NULL;
    ok = encoded && strlen(encoded) <= LX_AUDIO_META_MAX &&
         cJSON_AddStringToObject(entry, "audio", encoded) &&
         cJSON_AddStringToObject(entry, "controls_type", "audio") &&
         cJSON_AddBoolToObject(entry, "controls_blocked", blocked) &&
         cJSON_AddStringToObject(entry, "controls", encoded);
    free(encoded);
    cJSON_Delete(meta);
    return ok;
}

const cJSON *lx_audio_catalog_find(const cJSON *catalog, const char *control_uuid)
{
    if (!lx_uuid_valid(control_uuid))
        return NULL;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, catalog)
        if (!strcmp(audio_string(entry, "type"), "AudioZoneV2") &&
            !strcmp(audio_string(entry, "control_uuid"), control_uuid) &&
            audio_string(entry, "audio")[0])
            return entry;
    return NULL;
}

static bool audio_copy_state(char out[LX_UUID_LEN], const cJSON *meta, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(meta, key);
    if (!cJSON_IsString(value) || (value->valuestring[0] && !lx_uuid_valid(value->valuestring)))
        return false;
    snprintf(out, LX_UUID_LEN, "%s", value->valuestring);
    return true;
}

bool lx_audio_endpoint(const cJSON *entry, lx_audio_endpoint_t *out)
{
    if (!entry || !out || strcmp(audio_string(entry, "type"), "AudioZoneV2"))
        return false;
    const char *encoded = audio_string(entry, "audio");
    cJSON *meta = strlen(encoded) <= LX_AUDIO_META_MAX ? cJSON_Parse(encoded) : NULL;
    lx_audio_endpoint_t result = {0};
    unsigned unused_port;
    bool ok = cJSON_IsObject(meta) &&
              audio_host(audio_string(meta, "host"), result.host, &unused_port) &&
              audio_uint(cJSON_GetObjectItemCaseSensitive(meta, "port"), &result.port) &&
              result.port > 0 && result.port <= 65535 &&
              audio_uint(cJSON_GetObjectItemCaseSensitive(meta, "player"), &result.player) && result.player &&
              audio_uint(cJSON_GetObjectItemCaseSensitive(meta, "volumeModes"), &result.volume_modes) &&
              result.volume_modes <= 7 &&
              audio_copy_state(result.action, meta, "action") && result.action[0] &&
              audio_copy_state(result.locked_state, meta, "isLocked") &&
              audio_copy_state(result.lock_state, meta, "jLocked") &&
              audio_copy_state(result.volume_state, meta, "volume") &&
              audio_copy_state(result.max_volume_state, meta, "maxVolume") &&
              audio_copy_state(result.play_state, meta, "playState") &&
              audio_copy_state(result.server_state, meta, "serverState") &&
              audio_copy_state(result.client_state, meta, "clientState");
    result.blocked = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(meta, "blocked")) ||
                     !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "controls_blocked")) ||
                     !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(entry, "secured"));
    cJSON_Delete(meta);
    if (ok)
        *out = result;
    return ok;
}

bool lx_audio_command_build(const cJSON *entry, lx_audio_action_t action,
                            unsigned value, char out[96])
{
    lx_audio_endpoint_t endpoint;
    if (!out || !lx_audio_endpoint(entry, &endpoint) || endpoint.blocked)
        return false;
    const char *command = NULL;
    switch (action)
    {
        case LX_AUDIO_PLAY: command = "play"; break;
        case LX_AUDIO_PAUSE: command = "Pause"; break;
        case LX_AUDIO_PREVIOUS: command = "prev"; break;
        case LX_AUDIO_NEXT: command = "next"; break;
        case LX_AUDIO_VOLUME_UP: command = "volUp"; break;
        case LX_AUDIO_VOLUME_DOWN: command = "volDown"; break;
        case LX_AUDIO_VOLUME:
            if (value > 100 || (endpoint.volume_modes & 4U))
                return false;
            return snprintf(out, 96, "jdev/sps/io/%s/volume/%u", endpoint.action, value) < 96;
        default: return false;
    }
    return snprintf(out, 96, "jdev/sps/io/%s/%s", endpoint.action, command) < 96;
}

bool lx_audio_native_command(unsigned player, lx_audio_action_t action,
                             unsigned value, char out[96])
{
    if (!player || !out || action != LX_AUDIO_FAVORITE || !value)
        return false;
    return snprintf(out, 96, "audio/%u/roomfav/play/%u", player, value) < 96;
}

bool lx_audio_cover_url(const char *url)
{
    if (!url || !url[0] || strlen(url) >= LX_AUDIO_URL_SIZE)
        return false;
    const char *host = !strncmp(url, "https://", 8) ? url + 8 :
                       !strncmp(url, "http://", 7) ? url + 7 : NULL;
    if (!host || !host[0] || host[0] == '/')
        return false;
    const char *end = strchr(host, '/');
    if (!end)
        end = host + strlen(host);
    for (const char *p = url; *p; ++p)
        if ((unsigned char)*p <= 32 || *p == '\\' || *p == 127)
            return false;
    for (const char *p = host; p < end; ++p)
        if (*p == '@' || *p == '?' || *p == '#')
            return false;
    return true;
}

static bool audio_text_update(char *out, size_t capacity, const cJSON *row, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(row, key);
    if (!value)
        return false;
    const char *text = cJSON_IsString(value) && value->valuestring ? value->valuestring : "";
    if (strlen(text) >= capacity)
        text = "";
    if (!strcmp(out, text))
        return false;
    snprintf(out, capacity, "%s", text);
    return true;
}

bool lx_audio_track_update(lx_audio_track_t *track, unsigned player, const cJSON *message)
{
    if (!track || !player)
        return false;
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(message, "audio_event");
    if (!rows)
        rows = cJSON_GetObjectItemCaseSensitive(message, "status_result");
    if (!cJSON_IsArray(rows))
        return false;
    bool changed = false, artwork_changed = false;
    const cJSON *row;
    cJSON_ArrayForEach(row, rows)
    {
        unsigned id = 0;
        if (!audio_uint(cJSON_GetObjectItemCaseSensitive(row, "playerid"), &id) || id != player)
            continue;
        artwork_changed |= audio_text_update(track->title, sizeof(track->title), row, "title");
        artwork_changed |= audio_text_update(track->artist, sizeof(track->artist), row, "artist");
        artwork_changed |= audio_text_update(track->album, sizeof(track->album), row, "album");
        artwork_changed |= audio_text_update(track->station, sizeof(track->station), row, "station");
        artwork_changed |= audio_text_update(track->cover, sizeof(track->cover), row, "coverurl");
        if (track->cover[0] && !lx_audio_cover_url(track->cover))
        {
            track->cover[0] = 0;
            artwork_changed = true;
        }
        const cJSON *mode = cJSON_GetObjectItemCaseSensitive(row, "mode");
        if (mode)
        {
            const char *text = cJSON_IsString(mode) ? mode->valuestring : "";
            lx_audio_mode_t next = !strcmp(text, "play") ? LX_AUDIO_MODE_PLAY :
                !strcmp(text, "pause") ? LX_AUDIO_MODE_PAUSE :
                !strcmp(text, "stop") ? LX_AUDIO_MODE_STOP : LX_AUDIO_MODE_UNKNOWN;
            changed |= track->mode != next;
            track->mode = next;
        }
        const cJSON *volume = cJSON_GetObjectItemCaseSensitive(row, "volume");
        if (volume)
        {
            unsigned number = 0;
            bool valid = audio_uint(volume, &number) && number <= 100;
            changed |= track->volume_known != valid || (valid && track->volume != number);
            track->volume_known = valid;
            if (valid)
                track->volume = number;
        }
    }
    if (artwork_changed)
    {
        /* Azonos URL más zeneszámnál új kép lehet. Az azonos tartalmú zónák
         * viszont megoszthatják a cache-t: ezért tartalomkulcs, nem sorszám. */
        uint32_t hash = 2166136261U;
        const char *fields[] = {track->cover, track->title, track->artist, track->album, track->station};
        for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        {
            for (const unsigned char *p = (const unsigned char *)fields[i]; *p; ++p)
                hash = (hash ^ *p) * 16777619U;
            hash = (hash ^ 0xffU) * 16777619U;
        }
        track->version = hash ? hash : 1;
    }
    return changed || artwork_changed;
}

bool lx_audio_favorites_parse(lx_audio_favorites_t *out, unsigned player,
                              unsigned requested_start, const cJSON *message)
{
    if (!out || !player)
        return false;
    const cJSON *groups = cJSON_GetObjectItemCaseSensitive(message, "getroomfavs_result");
    if (!cJSON_IsArray(groups) || cJSON_GetArraySize(groups) != 1)
        return false;
    const cJSON *group = cJSON_GetArrayItem(groups, 0);
    unsigned id = 0, start = 0, total = 0;
    const cJSON *rows = cJSON_GetObjectItemCaseSensitive(group, "items");
    if (!audio_uint(cJSON_GetObjectItemCaseSensitive(group, "id"), &id) || id != player ||
        !audio_uint(cJSON_GetObjectItemCaseSensitive(group, "start"), &start) || start != requested_start ||
        !audio_uint(cJSON_GetObjectItemCaseSensitive(group, "totalitems"), &total) ||
        !cJSON_IsArray(rows) || start > total ||
        (unsigned)cJSON_GetArraySize(rows) > LX_AUDIO_FAVORITE_PAGE ||
        (unsigned)cJSON_GetArraySize(rows) > total - start ||
        (start < total && !cJSON_GetArraySize(rows)))
        return false;
    /* A nagy lista egyetlen, korlátos oldalát tároljuk. A felület a következő
     * ablakot kéri, nem csöndben levágja a 33. és további kedvenceket. */
    lx_audio_favorites_t *next = lx_light_alloc(1, sizeof(*next));
    if (!next)
        return false;
    next->player = player;
    next->start = start;
    next->total = total;
    bool ok = true;
    const cJSON *row;
    cJSON_ArrayForEach(row, rows)
    {
        lx_audio_favorite_t *item = &next->items[next->count];
        unsigned slot = 0;
        item->slot = -1;
        const char *name = audio_string(row, "name");
        if (!name[0])
            name = audio_string(row, "title");
        if (!audio_uint(cJSON_GetObjectItemCaseSensitive(row, "id"), &item->id) || !item->id)
        {
            ok = false;
            break;
        }
        for (unsigned j = 0; j < next->count; ++j)
            if (next->items[j].id == item->id)
                ok = false;
        if (!ok)
            break;
        if (audio_uint(cJSON_GetObjectItemCaseSensitive(row, "slot"), &slot) && slot <= INT_MAX)
            item->slot = (int)slot;
        if (name[0])
            light_copy(item->name, sizeof(item->name), name);
        else
            snprintf(item->name, sizeof(item->name), "Kedvenc %u", item->id);
        ++next->count;
    }
    if (ok)
    {
        next->ready = true;
        next->generation = out->generation + 1;
        *out = *next;
    }
    free(next);
    return ok;
}

bool lx_audio_session_token(const char *greeting, char out[LX_AUDIO_SESSION_SIZE])
{
    if (!greeting || !out || strncmp(greeting, "LWSS ", 5))
        return false;
    const char *start = strstr(greeting, "Session-Token:");
    if (!start)
        return false;
    start += strlen("Session-Token:");
    while (*start == ' ')
        ++start;
    size_t length = strcspn(start, "\r\n");
    while (length && start[length - 1] == ' ')
        --length;
    if (!length || length >= LX_AUDIO_SESSION_SIZE)
        return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)start[i] < 33 || start[i] == ':')
            return false;
    memcpy(out, start, length);
    out[length] = 0;
    return true;
}
