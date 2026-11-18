#include "app_inverter_http_internal.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMBER cJSON_GetObjectItemCaseSensitive

static bool equal(const cJSON *node, const char *text)
{
    return cJSON_IsString(node) && !strcmp(node->valuestring, text);
}

static cJSON *post_object(const app_inverter_config_t *cfg, const char *domain, const char *path,
                          const cJSON *object, const inv_http_header_t *headers, size_t count,
                          char *token, size_t size)
{
    char *body = cJSON_PrintUnformatted(object);
    if (!body) return NULL;
    cJSON *reply = inv_http_fetch(cfg, domain, path, 443, true, body, headers, count, token, size);
    mbedtls_platform_zeroize(body, strlen(body));
    cJSON_free(body);
    return reply;
}

static bool fronius(const app_inverter_config_t *cfg, app_energy_sample_t *s, int64_t *epoch)
{
    char *id = inv_http_escape(cfg->device), *path = NULL;
    if (!id) return false;
    int count = asprintf(&path, "/swqapi/pvsystems/%s/flowdata", id);
    free(id);
    if (count < 0) return false;
    const inv_http_header_t headers[] = {{"Accept", "application/json"},
                                         {"AccessKeyId", cfg->key}, {"AccessKeyValue", cfg->secret}};
    cJSON *root = inv_http_fetch(cfg, "solarweb.com", path, 443, true, NULL, headers, 3, NULL, 0);
    free(path);
    const cJSON *data = MEMBER(root, "data"), *channels = MEMBER(data, "channels");
    bool pv = false;
    double output_kw = NAN;
    if (equal(MEMBER(root, "pvSystemId"), cfg->device) && cJSON_IsArray(channels) &&
        cJSON_IsTrue(MEMBER(MEMBER(root, "status"), "isOnline"))) {
        *epoch = inv_http_timestamp(cJSON_GetStringValue(MEMBER(data, "logDateTime")), false);
        const cJSON *item;
        cJSON_ArrayForEach(item, channels) {
            const cJSON *name = MEMBER(item, "channelName"), *unit = MEMBER(item, "unit");
            if (equal(unit, "%") && equal(name, "BattSOC") && cfg->battery)
                inv_http_soc(MEMBER(item, "value"), s);
            double kw;
            if (!equal(unit, "W") || !inv_http_scale(item, "value", 0.001, &kw)) continue;
            if (equal(name, "PowerPV")) {
                s->power_pv_kw = kw;
                pv = true;
            } else if (equal(name, "PowerOutput")) {
                output_kw = kw;
            } else if (equal(name, "PowerLoad")) {
                s->power_full_kw = -kw;
                s->home_unavailable = false;
            } else if (equal(name, "PowerFeedIn")) {
                s->power_grid_kw = kw;
                s->grid_unavailable = false;
            } else if (cfg->battery && equal(name, "PowerBattCharge")) {
                s->power_bat_kw = kw;
                s->battery_power_unavailable = false;
            }
        }
        /* Mérő nélküli string inverter esetén a gyártó csak a termelt AC-
         * teljesítményt adja. Akkus rendszernél ez kisütést is tartalmazhat. */
        if (!pv && !cfg->battery && s->grid_unavailable && s->home_unavailable && isfinite(output_kw)) {
            s->power_pv_kw = fmax(0, output_kw);
            pv = true;
        }
    }
    cJSON_Delete(root);
    return pv;
}

static bool solax(const app_inverter_config_t *cfg, app_energy_sample_t *s, int64_t *epoch)
{
    cJSON *request = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(request, "wifiSn", cfg->device)) {
        cJSON_Delete(request);
        return false;
    }
    const inv_http_header_t headers[] = {{"Content-Type", "application/json"}, {"tokenId", cfg->secret}};
    cJSON *root = post_object(cfg, "solaxcloud.com", "/api/v2/dataAccess/realtimeInfo/get",
                              request, headers, 2, NULL, 0);
    cJSON_Delete(request);
    const cJSON *r = MEMBER(root, "result");
    bool pv = false;
    if (cJSON_IsTrue(MEMBER(root, "success")) && equal(MEMBER(r, "sn"), cfg->device)) {
        *epoch = inv_http_timestamp(cJSON_GetStringValue(MEMBER(r, "utcDateTime")), false);
        if (!*epoch) *epoch = inv_http_timestamp(cJSON_GetStringValue(MEMBER(r, "uploadTime")), true);
        static const char *const mppt[] = {"powerdc1","powerdc2","powerdc3","powerdc4"};
        for (size_t i = 0; i < 4; i++) {
            double kw;
            if (inv_http_scale(r, mppt[i], 0.001, &kw) && kw >= 0) {
                pv = true;
                s->power_pv_kw += kw;
            }
        }
        s->grid_unavailable = !inv_http_scale(r, "feedinpower", -0.001, &s->power_grid_kw);
        double ac;
        if (inv_http_scale(r, "acpower", 0.001, &ac)) inv_http_home_from_ac(s, ac);
        if (cfg->battery && !equal(MEMBER(r, "batStatus"), "2")) {
            inv_http_soc(MEMBER(r, "soc"), s);
            s->battery_power_unavailable = !inv_http_scale(r, "batPower", -0.001, &s->power_bat_kw);
        }
        const cJSON *status = MEMBER(r, "inverterStatus");
        if (equal(status, "103") || equal(status, "104")) s->status = 4;
        if (equal(status, "107")) s->status = 3;
    }
    cJSON_Delete(root);
    return pv;
}

/* Ezt a kontextust kizárólag a szinkronizáló feladat birtokolja.
 * Új profil/Jelszó vagy lejárt munkamenet esetén elengedi. */
#define HUAWEI_TOKEN_SIZE 512
static char *s_huawei_token;

void app_inverter_http_reset(void)
{
    if (s_huawei_token) {
        mbedtls_platform_zeroize(s_huawei_token, HUAWEI_TOKEN_SIZE);
        free(s_huawei_token);
        s_huawei_token = NULL;
    }
}

static bool huawei_login(const app_inverter_config_t *cfg)
{
    cJSON *credentials = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(credentials, "userName", cfg->user) ||
        !cJSON_AddStringToObject(credentials, "systemCode", cfg->secret)) {
        cJSON_Delete(credentials);
        return false;
    }
    s_huawei_token = calloc(1, HUAWEI_TOKEN_SIZE);
    const inv_http_header_t headers[] = {{"Content-Type", "application/json"}};
    cJSON *reply = NULL;
    if (s_huawei_token)
        reply = post_object(cfg, "fusionsolar.huawei.com", "/thirdData/login", credentials,
                            headers, 1, s_huawei_token, HUAWEI_TOKEN_SIZE);
    cJSON_Delete(credentials);
    bool ok = cJSON_IsTrue(MEMBER(reply, "success")) && s_huawei_token && *s_huawei_token;
    cJSON_Delete(reply);
    if (!ok) app_inverter_http_reset();
    return ok;
}

static bool huawei(const app_inverter_config_t *cfg, app_energy_sample_t *s, int64_t *epoch)
{
    if (!s_huawei_token && !huawei_login(cfg)) return false;
    /* Egy inverter és rövid idősáv. A napi, akár 288 nagy rekordot tartalmazó
     * válasz nem férne a panel korlátozott JSON-keretébe. */
    time_t now = time(NULL);
    cJSON *request = cJSON_CreateObject();
    if (!cJSON_AddStringToObject(request, "sns", cfg->device) ||
        !cJSON_AddNumberToObject(request, "devTypeId", 38) ||
        !cJSON_AddNumberToObject(request, "startTime", (double)(now - 900) * 1000) ||
        !cJSON_AddNumberToObject(request, "endTime", (double)now * 1000)) {
        cJSON_Delete(request);
        return false;
    }
    const inv_http_header_t headers[] = {{"Content-Type", "application/json"}, {"XSRF-TOKEN", s_huawei_token}};
    cJSON *root = post_object(cfg, "fusionsolar.huawei.com", "/thirdData/getDevHistoryKpi",
                              request, headers, 2, NULL, 0);
    cJSON_Delete(request);
    if (!cJSON_IsTrue(MEMBER(root, "success"))) {
        /* HTTP-hiba és failCode=305 után csak a következő szabályos ciklusban
         * lépünk újra be; a hibás jelszó nem okoz gyors belépési hurkot. */
        app_inverter_http_reset();
        cJSON_Delete(root);
        return false;
    }
    const cJSON *rows = MEMBER(root, "data"), *row;
    if (cJSON_IsArray(rows)) {
        cJSON_ArrayForEach(row, rows) {
            double ms, kw;
            const cJSON *sn = MEMBER(row, "sn"), *map = MEMBER(row, "dataItemMap");
            if ((sn && !equal(sn, cfg->device)) || !inv_http_scale(row, "collectTime", 1, &ms) ||
                ms <= 0 || ms > (double)now * 1000 + 1000 || !inv_http_scale(map, "mppt_power", 1, &kw)) continue;
            int64_t t = (int64_t)(ms / 1000);
            if (t <= *epoch || kw < 0) continue;
            *epoch = t;
            s->power_pv_kw = kw;
            double state;
            s->status = inv_http_scale(map, "inverter_state", 1, &state) && state == 768 ? 4 : 1;
        }
    }
    cJSON_Delete(root);
    return *epoch > 0;
}

static bool aiswei_signature(const app_inverter_config_t *cfg, const char *path, char signature[48])
{
    char *canonical = NULL;
    if (asprintf(&canonical, "GET\napplication/json\n\napplication/json; charset=UTF-8\n\nX-Ca-Key:%s\n%s",
                 cfg->user, path) < 0) return false;
    unsigned char digest[32];
    const mbedtls_md_info_t *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool ok = sha && mbedtls_md_hmac(sha, (const unsigned char *)cfg->secret, strlen(cfg->secret),
                                     (const unsigned char *)canonical, strlen(canonical), digest) == 0;
    mbedtls_platform_zeroize(canonical, strlen(canonical));
    free(canonical);
    size_t n = 0;
    if (ok) ok = mbedtls_base64_encode((unsigned char *)signature, 48, &n, digest, sizeof(digest)) == 0;
    mbedtls_platform_zeroize(digest, sizeof(digest));
    return ok && n == 44;
}

static bool aiswei_read_day(const app_inverter_config_t *cfg, const char *date, time_t now,
                            app_energy_sample_t *s, int64_t *epoch)
{
    char *key = inv_http_escape(cfg->key), *path = NULL;
    if (!key) return false;
    int n = asprintf(&path, "/getPlantOutput?date=%s&key=%s&period=bydays", date, key);
    free(key);
    if (n < 0) return false;
    char signature[48];
    if (!aiswei_signature(cfg, path, signature)) {
        free(path);
        return false;
    }
    const inv_http_header_t headers[] = {{"Accept", "application/json"},
        {"Content-Type", "application/json; charset=UTF-8"}, {"X-Ca-Signature-Headers", "X-Ca-Key"},
        {"X-Ca-Key", cfg->user}, {"X-Ca-Signature", signature}};
    cJSON *root = inv_http_fetch(cfg, "aisweicloud.com", path, 443, true, NULL, headers, 5, NULL, 0);
    mbedtls_platform_zeroize(signature, sizeof(signature));
    free(path);
    const cJSON *rows = MEMBER(root, "data"), *unit = MEMBER(root, "dataunit"), *row;
    if (cJSON_IsArray(rows) && (equal(unit, "W") || equal(unit, "kW") || equal(unit, "KW"))) {
        cJSON_ArrayForEach(row, rows) {
            const char *clock = cJSON_GetStringValue(MEMBER(row, "time"));
            if (!clock || (strlen(clock) != 5 && strlen(clock) != 8)) continue;
            char iso[24];
            snprintf(iso, sizeof(iso), "%.10s %.8s%s", date, clock, strlen(clock) == 5 ? ":00" : "");
            int64_t t = inv_http_timestamp(iso, true);
            if (!t || t > now || t <= *epoch) continue;
            const cJSON *value = MEMBER(row, "value");
            double power;
            if (!inv_http_number(value, &power)) {
                const char *str = cJSON_GetStringValue(value);
                char *end;
                if (!str || !*str) continue;
                power = strtod(str, &end);
                if (end == str || *end || !isfinite(power)) continue;
            }
            if (power < 0) continue;
            s->power_pv_kw = equal(unit, "W") ? power * 0.001 : power;
            *epoch = t;
        }
    }
    cJSON_Delete(root);
    return *epoch > 0;
}

static bool aiswei(const app_inverter_config_t *cfg, app_energy_sample_t *s, int64_t *epoch)
{
    char date[16];
    time_t now = time(NULL);
    struct tm local;
    if (!localtime_r(&now, &local) || !strftime(date, sizeof(date), "%Y-%m-%d", &local)) return false;
    return aiswei_read_day(cfg, date, now, s, epoch);
}

bool inv_http_cloud(const app_inverter_config_t *cfg, app_energy_sample_t *s, int64_t *epoch)
{
    if (!strcmp(cfg->profile, "fronius_cloud")) return fronius(cfg, s, epoch);
    if (!strcmp(cfg->profile, "solax_cloud")) return solax(cfg, s, epoch);
    if (!strcmp(cfg->profile, "huawei_cloud")) return huawei(cfg, s, epoch);
    if (!strcmp(cfg->profile, "solplanet_cloud")) return aiswei(cfg, s, epoch);
    return false;
}
