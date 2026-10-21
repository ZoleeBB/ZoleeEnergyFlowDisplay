/* A web csak a hitelesített identitások pillanatképét szerkeszti. NVS/BLE I/O
 * nincs a HTTP-taskban; a tartós mentést a meglévő háttérszolgáltatás végzi. */
#include "app_slave_presence_service.h"
#include <math.h>

#define SLAVE_PROFILE_JSON_MAX 2048

static cJSON *slave_presence_json(void)
{
    app_slave_presence_snapshot_t snapshot = {0};
    bool ready = s_config.data_seed_enabled && app_slave_presence_service_snapshot(&snapshot);
    cJSON *root = cJSON_CreateObject();
    cJSON *profiles = cJSON_CreateArray();
    if (!root || !profiles)
    {
        cJSON_Delete(root);
        cJSON_Delete(profiles);
        return NULL;
    }
    if (!cJSON_AddItemToObject(root, "profiles", profiles))
    {
        cJSON_Delete(profiles);
        cJSON_Delete(root);
        return NULL;
    }
    if (!cJSON_AddNumberToObject(root, "schema", 1) ||
        !cJSON_AddBoolToObject(root, "enabled", s_config.data_seed_enabled) ||
        !cJSON_AddBoolToObject(root, "ready", ready) ||
        !cJSON_AddNumberToObject(root, "revision", snapshot.revision) ||
        !cJSON_AddNumberToObject(root, "saved_revision", snapshot.saved_revision) ||
        !cJSON_AddNumberToObject(root, "save_error", snapshot.save_error))
        goto failed;
    for (unsigned i = 0; ready && i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        const app_slave_presence_peer_t *peer = &snapshot.table.peers[i];
        if (!peer->used)
            continue;
        char id[APP_SLAVE_PRESENCE_ID_SIZE];
        cJSON *item = cJSON_CreateObject();
        if (!item || !app_slave_identity_id(&peer->identity, id) ||
            !cJSON_AddStringToObject(item, "id", id) ||
            !cJSON_AddStringToObject(item, "name", peer->profile.name) ||
            !cJSON_AddStringToObject(item, "presence_uuid", peer->profile.presence_uuid) ||
            !cJSON_AddNumberToObject(item, "timeout_seconds", peer->profile.timeout_seconds) ||
            !cJSON_AddBoolToObject(item, "blank_at_night", peer->profile.blank_at_night) ||
            !cJSON_AddItemToArray(profiles, item))
        {
            cJSON_Delete(item);
            goto failed;
        }
    }
    return root;
failed:
    cJSON_Delete(root);
    return NULL;
}

static esp_err_t slave_presence_get(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    cJSON *root = slave_presence_json();
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                    "A Slave-eszközlista nem állítható össze.");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, body);
    free(body);
    return result;
}

static bool slave_fields(const cJSON *object, const char *const *fields, unsigned count)
{
    if (!cJSON_IsObject(object) || cJSON_GetArraySize(object) != (int)count)
        return false;
    /* Pontos mezőkészlet: a duplikált/idegen kulcs és a hiányzó mező is hiba. */
    for (unsigned i = 0; i < count; ++i)
        if (!cJSON_GetObjectItemCaseSensitive(object, fields[i]))
            return false;
    return true;
}

static bool slave_integer(const cJSON *value, uint32_t minimum, uint32_t maximum)
{
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
           value->valuedouble >= minimum && value->valuedouble <= maximum &&
           floor(value->valuedouble) == value->valuedouble;
}

static bool slave_json_text(const char *body, size_t size)
{
    /* A cJSON szövegei NUL-végűek: a beágyazott U+0000-t még előtte tiltjuk.
     * Az escape-elt backslash utáni szó szerinti "u0000" továbbra is szöveg. */
    for (size_t i = 0; i < size; ++i)
    {
        if (body[i] != '\\')
            continue;
        if (i + 5 < size && !memcmp(body + i + 1, "u0000", 5))
            return false;
        ++i;
    }
    return true;
}

static esp_err_t slave_presence_post(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    char header[8];
    if (httpd_req_get_hdr_value_str(req, "X-Zolee-Loxone", header, sizeof(header)) != ESP_OK ||
        strcmp(header, "1") || req->content_len < 2 || req->content_len > SLAVE_PROFILE_JSON_MAX)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen Slave-kérés.");
    app_slave_presence_snapshot_t snapshot;
    if (!s_config.data_seed_enabled || !app_slave_presence_service_snapshot(&snapshot))
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "A Master adat seed és a Slave-profil nem érhető el.");
    }
    char *body = calloc(1, req->content_len + 1);
    if (!body)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Nincs elegendő memória.");
    size_t position = 0;
    while (position < req->content_len)
    {
        int count = httpd_req_recv(req, body + position, req->content_len - position);
        if (count <= 0 || (size_t)count > req->content_len - position)
        {
            free(body);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "A kérés megszakadt.");
        }
        position += (size_t)count;
    }
    cJSON *root = slave_json_text(body, req->content_len) ?
                  lx_home_document_parse(body, req->content_len) : NULL;
    free(body);
    const char *const root_fields[] = {"schema", "revision", "id", "profile"};
    const char *const profile_fields[] = {"name", "presence_uuid", "timeout_seconds", "blank_at_night"};
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON *revision = cJSON_GetObjectItemCaseSensitive(root, "revision");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "profile");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(data, "name");
    const cJSON *uuid = cJSON_GetObjectItemCaseSensitive(data, "presence_uuid");
    const cJSON *timeout = cJSON_GetObjectItemCaseSensitive(data, "timeout_seconds");
    const cJSON *blank = cJSON_GetObjectItemCaseSensitive(data, "blank_at_night");
    app_slave_presence_profile_t profile = {0};
    bool valid = slave_fields(root, root_fields, 4) && slave_fields(data, profile_fields, 4) &&
                 slave_integer(schema, 1, 1) && slave_integer(revision, 1, UINT32_MAX) &&
                 cJSON_IsString(id) && strlen(id->valuestring) == APP_SLAVE_PRESENCE_ID_SIZE - 1 &&
                 cJSON_IsString(name) && strlen(name->valuestring) < sizeof(profile.name) &&
                 cJSON_IsString(uuid) && strlen(uuid->valuestring) < sizeof(profile.presence_uuid) &&
                 slave_integer(timeout, 1, 86400) && cJSON_IsBool(blank);
    if (valid)
    {
        memcpy(profile.name, name->valuestring, strlen(name->valuestring));
        memcpy(profile.presence_uuid, uuid->valuestring, strlen(uuid->valuestring));
        profile.timeout_seconds = (uint32_t)timeout->valuedouble;
        profile.blank_at_night = cJSON_IsTrue(blank);
        valid = app_slave_presence_profile_valid(&profile);
    }
    int index = -1;
    for (unsigned i = 0; valid && i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        char key[APP_SLAVE_PRESENCE_ID_SIZE];
        if (snapshot.table.peers[i].used &&
            app_slave_identity_id(&snapshot.table.peers[i].identity, key) &&
            !strcmp(key, id->valuestring))
            index = (int)i;
    }
    uint32_t expected = valid ? (uint32_t)revision->valuedouble : 0;
    cJSON_Delete(root);
    if (!valid || index < 0)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                    "Ellenőrizd a párosított eszközt, a nevet, a jelenlétforrást és az időkorlátot.");
    if (expected != snapshot.revision ||
        !app_slave_presence_service_update(&snapshot.table.peers[index].identity, &profile, expected))
    {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "A Slave-profil közben változott. Frissítsd a listát és ellenőrizd a szerkesztést.");
    }
    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"accepted\":true}");
}
