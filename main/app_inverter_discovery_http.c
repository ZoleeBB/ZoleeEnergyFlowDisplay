/* A helyi HTTP-profilok és a Victron GX egyszeri, csak olvasó azonosítása.
 * A keresés nem kap mentett belépési adatokat. Minden további GET az eredeti
 * IPv4-végpontra megy, ugyanazon abszolút határidőn belül. */
#include "app_inverter_discovery.h"
#include "app_inverter_http_internal.h"
#include "esp_heap_caps.h"
#include "http_parser.h"
#include "mdns.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define MEMBER cJSON_GetObjectItemCaseSensitive
#define BODY_LIMIT 12288U

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static bool ready(int fd, int64_t deadline, bool write)
{
    if (fd < 0) return false;
#ifndef ESP_PLATFORM
    if (fd >= FD_SETSIZE) return false;
#endif
    for (;;) {
        int64_t left = deadline - now_ms();
        if (left <= 0) return false;
        struct timeval wait = {.tv_sec = left / 1000, .tv_usec = left % 1000 * 1000};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        int rc = select(fd + 1, write ? NULL : &set, write ? &set : NULL, NULL, &wait);
        if (rc >= 0 || errno != EINTR) return rc > 0;
    }
}

typedef struct {
    struct sockaddr_in peer, local;
} probe_connection_t;

static int reconnect(const probe_connection_t *connection, int64_t deadline)
{
    if (now_ms() >= deadline) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 &&
        bind(fd, (const struct sockaddr *)&connection->local, sizeof(connection->local)) == 0) {
        if (!connect(fd, (const struct sockaddr *)&connection->peer, sizeof(connection->peer))) return fd;
        int err;
        socklen_t n = sizeof(err);
        if (errno == EINPROGRESS && ready(fd, deadline, true) &&
            !getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &n) && !err) return fd;
    }
    close(fd);
    return -1;
}

typedef struct {
    char *body;
    size_t size, capacity;
    bool complete;
} response_t;

static int body_part(http_parser *parser, const char *data, size_t len)
{
    response_t *response = parser->data;
    if (response->complete || parser->status_code != 200 || len > BODY_LIMIT - response->size) return -1;
    size_t need = response->size + len + 1;
    if (response->capacity < need) {
        size_t capacity = response->capacity ? response->capacity * 2 : 1024;
        if (capacity < need) capacity = need;
        if (capacity > BODY_LIMIT + 1) capacity = BODY_LIMIT + 1;
        char *buf = heap_caps_realloc(response->body, capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) return -1;
        response->body = buf;
        response->capacity = capacity;
    }
    memcpy(response->body + response->size, data, len);
    response->size += len;
    response->body[response->size] = 0;
    return 0;
}

static int complete(http_parser *parser)
{
    response_t *response = parser->data;
    if (response->complete || parser->status_code != 200) return -1;
    response->complete = true;
    return 0;
}

static bool send_get(int fd, int64_t deadline, const char *host, uint16_t port, const char *path)
{
    char request[320];
    int n = snprintf(request, sizeof(request),
                     "GET %s HTTP/1.1\r\nHost: %s:%u\r\nAccept: application/json\r\nConnection: close\r\n\r\n",
                     path, host, (unsigned)port);
    if (n < 0 || (size_t)n >= sizeof(request)) return false;
    size_t sent = 0;
    while (sent < (size_t)n && ready(fd, deadline, true)) {
        ssize_t size = send(fd, request + sent, (size_t)n - sent, 0);
        if (size < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (size <= 0) return false;
        sent += (size_t)size;
    }
    return sent == (size_t)n;
}

static cJSON *get(int fd, int64_t deadline, const app_inverter_discovery_result_t *endpoint,
                  const char *path)
{
    if (fd < 0) return NULL;
    response_t response = {0};
    http_parser parser;
    http_parser_init(&parser, HTTP_RESPONSE);
    parser.data = &response;
    static const http_parser_settings handlers = {.on_body=body_part, .on_message_complete=complete};
    bool ok = send_get(fd, deadline, endpoint->host, endpoint->port, path);
    size_t total = 0;
    while (ok && !response.complete && ready(fd, deadline, false)) {
        char part[456];
        ssize_t n = recv(fd, part, sizeof(part), 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0 || (total += (size_t)n) > BODY_LIMIT + 4096) break;
        ok = http_parser_execute(&parser, &handlers, part, (size_t)n) == (size_t)n &&
             HTTP_PARSER_ERRNO(&parser) == HPE_OK && !parser.upgrade;
        if (n == 0) break;
    }
    close(fd);
    cJSON *json = ok && response.complete ? app_inverter_json_parse(response.body, response.size) : NULL;
    free(response.body);
    return json;
}

/* Csak a sorozatszámokban előforduló ASCII karakterek kerülhetnek a kért
 * útvonalba vagy a később menthető eszközazonosítóba. Csonka ID nem használható. */
static bool identifier(const char *s, size_t cap)
{
    return s && *s && strlen(s) < cap &&
           s[strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.")] == 0;
}

static bool decimal_id(const char *s)
{
    return s && *s && strlen(s) <= 2 && (s[0] != '0' || !s[1]) && s[strspn(s, "0123456789")] == 0;
}

static bool unique_device(const cJSON *list, const cJSON *item, const char *serial_key, const char *serial)
{
    const cJSON *other;
    cJSON_ArrayForEach(other, list) {
        if (other == item) continue;
        if (item->string && other->string && !strcmp(other->string, item->string)) return false;
        const char *value = cJSON_GetStringValue(MEMBER(other, serial_key));
        if (value && !strcmp(value, serial)) return false;
    }
    return true;
}

static const cJSON *fronius_data(const cJSON *root)
{
    const cJSON *status = MEMBER(MEMBER(MEMBER(root, "Head"), "Status"), "Code");
    return cJSON_IsNumber(status) && status->valuedouble == 0 ? MEMBER(MEMBER(root, "Body"), "Data") : NULL;
}

static size_t fronius_results(int fd, int64_t deadline, const probe_connection_t *connection,
                              app_inverter_discovery_result_t *out, size_t capacity)
{
    cJSON *info = get(fd, deadline, out, "/solar_api/v1/GetInverterInfo.cgi");
    const cJSON *data = fronius_data(info);
    size_t count = 0;
    if (cJSON_IsObject(data) && cJSON_GetArraySize(data) <= 100) {
        const cJSON *item;
        cJSON_ArrayForEach(item, data) {
            const char *serial = cJSON_GetStringValue(MEMBER(item, "UniqueID"));
            const cJSON *dt = MEMBER(item, "DT");
            if (!decimal_id(item->string) || !cJSON_IsNumber(dt) || dt->valuedouble != 1 ||
                !identifier(serial, sizeof(out->serial)) || !unique_device(data, item, "UniqueID", serial)) continue;
            if (count < capacity) {
                app_inverter_discovery_result_t *r = &out[count];
                *r = out[0];
                strcpy(r->model, "Fronius Solar API");
                strcpy(r->serial, serial);
                strcpy(r->device, item->string);
                r->verified = false;
            }
            if (++count > capacity) break;
        }
    }
    cJSON_Delete(info);
    if (!count) return 0;
    cJSON *flow = get(reconnect(connection, deadline), deadline, out, "/solar_api/v1/GetPowerFlowRealtimeData.fcgi");
    data = fronius_data(flow);
    const cJSON *pv = MEMBER(MEMBER(data, "Site"), "P_PV");
    const cJSON *inverters = MEMBER(data, "Inverters");
    for (size_t i = 0; i < count && i < capacity; ++i)
        out[i].verified = cJSON_IsNumber(pv) && isfinite(pv->valuedouble) && pv->valuedouble >= 0 &&
                          pv->valuedouble * 0.001 <= APP_DIRECT_POWER_MAX_KW &&
                          cJSON_IsObject(MEMBER(inverters, out[i].device));
    cJSON_Delete(flow);
    return count;
}

/* A típusjelölés az ASW 8000-T adattáblán szóközzel is szerepelhet. Másik
 * név szerint azonosított típust az ASW8000-T profil nem ajánl fel. */
static bool asw8000(const char *model)
{
    const char *expected = "ASW8000-T";
    for (; *model; ++model) {
        if (*model == ' ') continue;
        if (!*expected || toupper((unsigned char)*model) != *expected++) return false;
    }
    return !*expected;
}

static bool solplanet_power(const cJSON *data, const char *serial)
{
    if (!inv_http_solplanet_matches(data, serial)) return false;
    double kw;
    if (inv_http_solplanet_dc(data, &kw)) return kw >= 0 && kw <= APP_DIRECT_POWER_MAX_KW;
    const cJSON *ac = MEMBER(data, "pac");
    /* Az ASW8000-T adatlapja 8000 VA maximális AC-kimenetet ad meg. */
    return cJSON_IsNumber(ac) && isfinite(ac->valuedouble) && ac->valuedouble >= 0 && ac->valuedouble <= 8000;
}

static size_t solplanet_results(int fd, int64_t deadline, const probe_connection_t *connection,
                                app_inverter_discovery_result_t *out, size_t capacity)
{
    cJSON *info = get(fd, deadline, out, "/getdev.cgi?device=2");
    bool v1 = !cJSON_IsArray(MEMBER(info, "inv"));
    if (v1) {
        cJSON_Delete(info);
        info = get(reconnect(connection, deadline), deadline, out, "/invinfo.cgi");
    }
    const cJSON *list = MEMBER(info, "inv"), *num = MEMBER(info, "num"), *item;
    size_t count = 0;
    if (cJSON_IsArray(list) && cJSON_IsNumber(num) && num->valuedouble <= 32 && num->valuedouble == cJSON_GetArraySize(list)) {
        cJSON_ArrayForEach(item, list) {
            const char *serial = cJSON_GetStringValue(MEMBER(item, "isn"));
            const char *model = cJSON_GetStringValue(MEMBER(item, "model"));
            bool known = model && *model;
            if (!identifier(serial, sizeof(out->serial)) || !unique_device(list, item, "isn", serial) ||
                (known ? !asw8000(model) : !cJSON_IsNumber(MEMBER(item, "mty")))) continue;
            if (count < capacity) {
                app_inverter_discovery_result_t *r = &out[count];
                *r = out[0];
                strcpy(r->model, known ? "Solplanet ASW8000-T" : "Solplanet (ismeretlen típus)");
                strcpy(r->serial, serial);
                strcpy(r->device, serial);
                r->verified = false;
                if (known) {
                    char path[120];
                    snprintf(path, sizeof(path), v1 ? "/invdata.cgi?sn=%s" :
                             "/getdevdata.cgi?device=2&sn=%s", serial);
                    cJSON *measurement = get(reconnect(connection, deadline), deadline, out, path);
                    r->verified = solplanet_power(measurement, serial);
                    cJSON_Delete(measurement);
                }
            }
            if (++count > capacity) break;
        }
    }
    cJSON_Delete(info);
    return count;
}

bool app_inverter_discovery_http_supported(const char *profile)
{
    return profile && (!strcmp(profile, "fronius_http") || !strcmp(profile, "solplanet_http"));
}

size_t app_inverter_discovery_http(const char *profile, int fd, uint32_t timeout_ms,
                                   app_inverter_discovery_result_t *out, size_t capacity)
{
    if (fd < 0) return 0;
    probe_connection_t connection;
    socklen_t peer_size = sizeof(connection.peer), local_size = sizeof(connection.local);
    struct in_addr expected;
    if (!app_inverter_discovery_http_supported(profile) || !timeout_ms || !out || !capacity ||
        getpeername(fd, (struct sockaddr *)&connection.peer, &peer_size) || connection.peer.sin_family != AF_INET ||
        getsockname(fd, (struct sockaddr *)&connection.local, &local_size) || connection.local.sin_family != AF_INET ||
        inet_pton(AF_INET, out->host, &expected) != 1 || expected.s_addr != connection.peer.sin_addr.s_addr ||
        out->port != ntohs(connection.peer.sin_port)) {
        close(fd);
        return 0;
    }
    /* A következő ellenőrző GET is ugyanarról a STA-címről induljon.
     * Az előző TCP-portot a kapcsolat lezárása után a rendszer még foglalhatja. */
    connection.local.sin_port = 0;
    int64_t deadline = now_ms() + (timeout_ms > 2000 ? 2000 : timeout_ms);
    return !strcmp(profile, "fronius_http") ? fronius_results(fd, deadline, &connection, out, capacity) :
                                             solplanet_results(fd, deadline, &connection, out, capacity);
}

/* Venus OS az UPnP UDN utolsó tagjába a VRM Portal ID hat hexadecimális
 * bájtját teszi. A LOCATION mezőben esetleg szereplő idegen URL-t sosem követjük. */
static bool gx_reply(char *packet, size_t size, char portal[13])
{
    static const char prefix[] = "uuid:com.victronenergy.ccgx.";
    static const char target[] = "upnp:rootdevice";
    if (memchr(packet, 0, size) || size < 19 || memcmp(packet, "HTTP/1.1 200 ", 13)) return false;
    char *line = strstr(packet, "\r\n");
    if (!line) return false;
    const char *usn = NULL, *st = NULL;
    for (;;) {
        line += 2;
        char *end = strstr(line, "\r\n");
        if (!end) return false;
        *end = 0;
        if (!*line) break;
        char *colon = strchr(line, ':');
        if (!colon) return false;
        *colon++ = 0;
        while (*colon == ' ' || *colon == '\t') ++colon;
        char *tail = end;
        while (tail > colon && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = 0;
        if (!strcasecmp(line, "USN")) {
            if (usn) return false;
            usn = colon;
        } else if (!strcasecmp(line, "ST")) {
            if (st) return false;
            st = colon;
        }
        line = end;
    }
    if (!usn || !st || strcasecmp(st, target) || strncasecmp(usn, prefix, sizeof(prefix) - 1)) return false;
    usn += sizeof(prefix) - 1;
    for (unsigned i = 0; i < 12; ++i) {
        if (i && i % 2 == 0 && *usn++ != ':') return false;
        if (!isxdigit((unsigned char)*usn)) return false;
        portal[i] = (char)tolower((unsigned char)*usn++);
    }
    portal[12] = 0;
    return !strncasecmp(usn, "::", 2) && !strcasecmp(usn + 2, target);
}

/* A Fronius név szerinti DNS-SD szolgáltatása a Solar API kapcsolójától
 * független jelöltet adhat. A végleges eszközindex a HTTP-válaszból jön. */
static size_t fronius_native(app_inverter_discovery_result_t *out, size_t capacity)
{
    static const char service[] = "_Fronius-SE-Inverter";
    mdns_result_t *results = NULL;
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!station) return 0;
    size_t count = 0;
    if (mdns_query_ptr(service, "_tcp", 850, capacity + 1, &results) == ESP_OK) {
        for (const mdns_result_t *r = results; r && count <= capacity; r = r->next) {
            if (r->esp_netif != station || !r->service_type || strcasecmp(r->service_type, service) ||
                !r->proto || strcasecmp(r->proto, "_tcp")) continue;
            for (const mdns_ip_addr_t *a = r->addr; a; a = a->next) {
                char host[INET_ADDRSTRLEN];
                if (a->addr.type != ESP_IPADDR_TYPE_V4 ||
                    !inet_ntop(AF_INET, &a->addr.u_addr.ip4.addr, host, sizeof(host))) continue;
                size_t i = 0;
                while (i < count && strcmp(out[i].host, host)) ++i;
                if (i < count) continue;
                if (count == capacity) {
                    ++count;
                    break;
                }
                app_inverter_discovery_result_t *row = &out[count++];
                memset(row, 0, sizeof(*row));
                strcpy(row->host, host);
                strcpy(row->model, "Fronius inverter (mDNS)");
                for (size_t j = 0; r->txt && r->txt_value_len && j < r->txt_count; ++j) {
                    const char *key = r->txt[j].key, *value = r->txt[j].value;
                    if (key && !strcasecmp(key, "DeviceSerialNumber") && r->txt_value_len[j] > 0 &&
                        r->txt_value_len[j] < sizeof(row->serial) && identifier(value, sizeof(row->serial)) &&
                        strlen(value) == r->txt_value_len[j]) strcpy(row->serial, value);
                }
            }
        }
    }
    if (results) mdns_query_results_free(results);
    return count;
}

size_t app_inverter_discovery_native(const char *profile, uint32_t local,
                                     app_inverter_discovery_result_t *out, size_t capacity)
{
    if (!profile || !local || !out || !capacity) return 0;
    if (!strcmp(profile, "fronius_http")) return fronius_native(out, capacity);
    bool mqtt = !strcmp(profile, "victron_mqtt");
    if (!mqtt && strcmp(profile, "victron_modbus")) return 0;
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return 0;
    static const char query[] = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                                "MAN: \"ssdp:discover\"\r\nMX: 1\r\nST: upnp:rootdevice\r\n\r\n";
    struct sockaddr_in destination = {.sin_family=AF_INET, .sin_port=htons(1900)};
    inet_pton(AF_INET, "239.255.255.250", &destination.sin_addr);
    struct sockaddr_in station = {.sin_family=AF_INET, .sin_addr.s_addr=local};
    unsigned char ttl = 1;
    size_t count = 0;
    char *packet = heap_caps_realloc(NULL, 1536, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (packet && !bind(fd, (const struct sockaddr *)&station, sizeof(station)) &&
        !setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &station.sin_addr, sizeof(station.sin_addr)) &&
        !setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) &&
        sendto(fd, query, sizeof(query) - 1, 0, (const struct sockaddr *)&destination,
               sizeof(destination)) == sizeof(query) - 1) {
        int64_t deadline = now_ms() + 1150;
        /* A feladó címét használjuk; a scheduler ellenőrzi, hogy az aktuális,
         * a Masterrel közös alhálózaton van-e. A GX találat a modul jelenlétét
         * igazolja, az MQTT vagy a Modbus engedélyezését külön kell ellenőrizni. */
        for (unsigned replies = 0; replies < 64 && ready(fd, deadline, false); ++replies) {
            struct sockaddr_in sender;
            socklen_t sender_len = sizeof(sender);
            ssize_t size = recvfrom(fd, packet, 1535, 0, (struct sockaddr *)&sender, &sender_len);
            if (size <= 0 || size >= 1535 || sender.sin_family != AF_INET) continue;
            packet[size] = 0;
            char portal[13], host[INET_ADDRSTRLEN];
            if (!gx_reply(packet, (size_t)size, portal) ||
                !inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host))) continue;
            size_t i = 0;
            while (i < count && strcmp(out[i].host, host)) ++i;
            if (i < count) continue;
            if (count == capacity) {
                ++count;
                break;
            }
            app_inverter_discovery_result_t *r = &out[count++];
            memset(r, 0, sizeof(*r));
            strcpy(r->host, host);
            strcpy(r->model, "Victron GX");
            if (mqtt) strcpy(r->device, portal);
        }
    }
    free(packet);
    close(fd);
    return count;
}
