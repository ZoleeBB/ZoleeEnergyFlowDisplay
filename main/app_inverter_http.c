#include "app_inverter_http_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *inv_http_escape(const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = strlen(value);
    char *text = malloc(3 * n + 1);
    if (!text) return NULL;
    char *p = text;
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (isalnum(ch) || strchr("-._~", ch)) *p++ = (char)ch;
        else {
            *p++ = '%';
            *p++ = hex[ch >> 4];
            *p++ = hex[ch & 15];
        }
    }
    *p = '\0';
    return text;
}

cJSON *inv_http_fetch(const app_inverter_config_t *cfg, const char *domain, const char *path,
                      unsigned default_port, bool tls, const char *body,
                      const inv_http_header_t *headers, size_t count, char *token, size_t token_size)
{
    /* Csak DNS-név vagy IPv4 fogadható el. Az útvonalat és a hitelesítési célgépet
     * nem módosíthatja az eszközazonosító vagy a beállításból származó szöveg. */
    const char *host = cfg->host;
    size_t n = strlen(host);
    if (!n || host[0] == '.' || host[n - 1] == '.') return NULL;
    for (size_t i = 0; i < n; i++) {
        if (!isalnum((unsigned char)host[i]) && host[i] != '-' && host[i] != '.') return NULL;
    }
    if (domain) {
        size_t d = strlen(domain);
        if (n < d || (n > d && host[n - d - 1] != '.') || strcasecmp(host + n - d, domain))
            return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        if (!headers[i][1] || strpbrk(headers[i][1], "\r\n")) return NULL;
    }
    char *url = NULL;
    if (asprintf(&url, "%s://%s:%u%s", tls ? "https" : "http", host,
                 cfg->port ? (unsigned)cfg->port : default_port, path) < 0) return NULL;
    cJSON *json = app_inverter_json_request(url, body ? "POST" : "GET", body, headers, count,
                                           token, token_size, domain ? NULL : cfg->trusted_pem);
    free(url);
    return json;
}

bool inv_http_number(const cJSON *node, double *value)
{
    if (cJSON_IsNumber(node) && isfinite(node->valuedouble)) {
        *value = node->valuedouble;
        return true;
    }
    return false;
}

bool inv_http_scale(const cJSON *obj, const char *key, double scale, double *value)
{
    double number;
    if (!inv_http_number(cJSON_GetObjectItemCaseSensitive(obj, key), &number)) return false;
    *value = number * scale;
    return isfinite(*value);
}

bool inv_http_soc(const cJSON *node, app_energy_sample_t *sample)
{
    double soc;
    if (!inv_http_number(node, &soc) || soc < 0 || soc > 100) return false;
    sample->soc_bat_pct = soc;
    sample->soc_unavailable = false;
    return true;
}

void inv_http_home_from_ac(app_energy_sample_t *sample, double ac_kw)
{
    if (sample->grid_unavailable) return;
    double load = ac_kw + sample->power_grid_kw;
    if (isfinite(load) && load >= -0.05) {
        sample->power_full_kw = fmax(0.0, load);
        sample->home_unavailable = false;
    }
}

static bool leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int digits(const char *s, unsigned count)
{
    int value = 0;
    for (unsigned i = 0; i < count; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        value = 10 * value + (s[i] - '0');
    }
    return value;
}

/* Az időzóna nélküli gyártói telephelyidőt kizárólag erre dokumentált mezőn
 * fogadjuk el. A helyi TZ-t sohasem állítjuk át a többi feladat alatt. */
int64_t inv_http_timestamp(const char *text, bool allow_site_local)
{
    if (!text || strlen(text) < 19 || text[4] != '-' || text[7] != '-' ||
        (text[10] != 'T' && text[10] != ' ') || text[13] != ':' || text[16] != ':') return 0;
    int y = digits(text, 4), m = digits(text + 5, 2), d = digits(text + 8, 2);
    int h = digits(text + 11, 2), min = digits(text + 14, 2), sec = digits(text + 17, 2);
    static const int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 2000 || y > 2130 || m < 1 || m > 12 || d < 1 ||
        d > month_days[m - 1] + (m == 2 && leap(y)) ||
        h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) return 0;
    const char *suffix = text + 19;
    if (*suffix == '.') {
        suffix++;
        if (!isdigit((unsigned char)*suffix)) return 0;
        while (isdigit((unsigned char)*suffix)) suffix++;
    }
    if (!*suffix && allow_site_local) {
        struct tm local = {.tm_year = y - 1900, .tm_mon = m - 1, .tm_mday = d,
                           .tm_hour = h, .tm_min = min, .tm_sec = sec, .tm_isdst = -1};
        time_t epoch = mktime(&local);
        return epoch > 0 && local.tm_hour == h && local.tm_min == min ? epoch : 0;
    }
    int offset = 0;
    if (strcmp(suffix, "Z")) {
        if (strlen(suffix) != 6 || (suffix[0] != '+' && suffix[0] != '-') || suffix[3] != ':') return 0;
        int oh = digits(suffix + 1, 2), om = digits(suffix + 4, 2);
        if (oh < 0 || oh > 14 || om < 0 || om > 59 || (oh == 14 && om)) return 0;
        offset = (oh * 3600 + om * 60) * (suffix[0] == '-' ? -1 : 1);
    }
    int days = d - 1;
    for (int year = 1970; year < y; year++) days += 365 + leap(year);
    for (int month = 1; month < m; month++) days += month_days[month - 1] + (month == 2 && leap(y));
    return (int64_t)days * 86400 + h * 3600 + min * 60 + sec - offset;
}

bool app_inverter_http_read(const app_inverter_config_t *cfg, app_energy_sample_t *out, int64_t *epoch)
{
    if (!cfg || !out || !epoch) return false;
    app_energy_sample_t sample = {.grid_unavailable = true, .home_unavailable = true,
                                  .battery_unavailable = !cfg->battery, .soc_unavailable = true,
                                  .battery_power_unavailable = true, .status = 1};
    bool cloud = strstr(cfg->profile, "_cloud") != NULL;
    int64_t t = 0;
    if (cloud ? (!inv_http_cloud(cfg, &sample, &t) || t <= 0) : !inv_http_local(cfg, &sample))
        return false;
    if (!isfinite(sample.power_pv_kw) || sample.power_pv_kw < -0.05) return false;
    sample.power_pv_kw = fmax(0, sample.power_pv_kw);
    *out = sample;
    *epoch = t;
    return true;
}
