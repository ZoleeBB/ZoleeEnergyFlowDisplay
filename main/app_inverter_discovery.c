/* Első párosítás a webes űrlapról. A keresés csak az adott profil
 * olvasható protokollját vizsgálja. Nem módosítja az adatforrást, az
 * aktív MAC-követő állapotát vagy az inverter beállításait. */
#include "app_inverter_discovery.h"
#include "app_inverter_settings.h"
#include "app_ota.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define SEARCH_US INT64_C(90000000)
#define RESULT_TTL_US INT64_C(60000000)
#define PROBE_MS 1600
#define CONNECT_MS 250
#define SEARCH_STACK_BYTES 8192

typedef struct {
    uint32_t own, mask, gateway;
} network_t;

typedef struct {
    app_inverter_discovery_status_t status;
    network_t net;
    uint16_t port;
    uint8_t unit;
    char device[96];
    bool cancel;
    int64_t expires, deadline;
    TaskHandle_t finished_task;
} search_t;

typedef struct {
    app_inverter_discovery_result_t native[APP_DISCOVERY_MAX_RESULTS + 1];
    app_inverter_discovery_result_t probe[APP_DISCOVERY_MAX_RESULTS];
} scratch_t;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static search_t *active;
static uint32_t sequence;
static _Atomic bool cached;

static bool solarman(const char *profile)
{
    return !strcmp(profile, "deye_v5") || !strcmp(profile, "sofar_v5");
}

bool app_inverter_discovery_supported(const char *profile)
{
    if (!profile || !app_inverter_profile(profile)) return false;
    return solarman(profile) || !strcmp(profile, "victron_mqtt") ||
           app_inverter_discovery_http_supported(profile) || app_inverter_discovery_modbus_supported(profile);
}

static bool network_now(network_t *out)
{
    app_runtime_state_t state;
    app_state_get_runtime(&state);
    if (state.data_recept_mode != APP_DATA_INVERTER || !state.wifi_connected || app_ota_operation_active())
        return false;
    esp_netif_ip_info_t ip;
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta || esp_netif_get_ip_info(sta, &ip) != ESP_OK) return false;
    out->own = ntohl(ip.ip.addr);
    out->mask = ntohl(ip.netmask.addr);
    out->gateway = ntohl(ip.gw.addr);
    uint32_t hosts = ~out->mask;
    return (out->own >> 24) && (out->own >> 24) != 127 && out->own < UINT32_C(0xe0000000) &&
           out->mask && !(hosts & (hosts + 1));
}

static bool eligible(const network_t *net, uint32_t address)
{
    uint32_t host = address & ~net->mask;
    return host && host != ~net->mask && address != net->own && address != net->gateway &&
           (address >> 24) && (address >> 24) != 127 && address < UINT32_C(0xe0000000) &&
           (address & net->mask) == (net->own & net->mask);
}

static bool continuing(search_t *s, app_inverter_discovery_state_t *end)
{
    if (*end != APP_DISCOVERY_DONE) return false;
    network_t now;
    portENTER_CRITICAL(&lock);
    bool cancelled = s->cancel;
    portEXIT_CRITICAL(&lock);
    if (cancelled) {
        *end = APP_DISCOVERY_CANCELLED;
        return false;
    }
    if (!network_now(&now) || now.own != s->net.own || now.mask != s->net.mask || now.gateway != s->net.gateway) {
        *end = APP_DISCOVERY_FAILED;
        return false;
    }
    if (esp_timer_get_time() < s->deadline) return true;
    portENTER_CRITICAL(&lock);
    s->status.limited = true;
    portEXIT_CRITICAL(&lock);
    return false;
}

static bool socket_ready(int fd, bool writing, int64_t until)
{
#ifndef ESP_PLATFORM
    if (fd < 0 || fd >= FD_SETSIZE) return false;
#endif
    for (;;) {
        int64_t left = until - esp_timer_get_time();
        if (left <= 0) return false;
        struct timeval tv = {.tv_sec = left / 1000000, .tv_usec = left % 1000000};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        int n = select(fd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &tv);
        if (n >= 0 || errno != EINTR) return n > 0;
    }
}

/* Forrás-IP-hez kötjük, így a Wi-Fi kiesésekor más interfészen nem próbál
 * ugyanazzal a címmel kapcsolatot létesíteni. */
static int station_socket(const network_t *net, int type)
{
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) return -1;
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(net->own)};
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int connect_peer(const search_t *s, uint32_t address, app_inverter_discovery_state_t *end)
{
    int fd = station_socket(&s->net, SOCK_STREAM);
    if (fd < 0) {
        *end = APP_DISCOVERY_FAILED;
        return -1;
    }
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(s->port),
                               .sin_addr.s_addr = htonl(address)};
    if (connect(fd, (struct sockaddr *)&peer, sizeof(peer)) == 0) return fd;
    int connect_error = errno;
    int64_t limit = esp_timer_get_time() + CONNECT_MS * 1000;
    if (connect_error == EINPROGRESS && socket_ready(fd, true, limit < s->deadline ? limit : s->deadline)) {
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && !error) return fd;
    }
    close(fd);
    return -1;
}

static bool mac_text(const char *mac)
{
    size_t len = strlen(mac);
    if (len != 12 && len != 17) return false;
    for (size_t i = 0; i < len; ++i) {
        if (len == 17 && i % 3 == 2) {
            if (mac[i] != ':' && mac[i] != '-') return false;
        } else if (!isxdigit((unsigned char)mac[i])) return false;
    }
    return true;
}

/* A válaszból kapott IP csak akkor hiteles címjelölt, ha a tényleges helyi
 * küldő címe is ugyanaz. A MAC itt a gyártói üzenet alakját ellenőrzi;
 * a futó adatforrás csak az első érvényes mintánál párosít. */
static bool logger_reply(const search_t *s, char *packet, uint32_t source, app_inverter_discovery_result_t *row)
{
    char *mac = strchr(packet, ',');
    if (!mac || !eligible(&s->net, source)) return false;
    *mac++ = 0;
    char *serial = strchr(mac, ',');
    if (!serial) return false;
    *serial++ = 0;
    uint32_t ip;
    if (inet_pton(AF_INET, packet, &ip) != 1 || ntohl(ip) != source || !mac_text(mac)) return false;
    size_t n = strlen(serial);
    while (n && (serial[n - 1] == '\r' || serial[n - 1] == '\n')) serial[--n] = 0;
    if (!n || n > 10 || strspn(serial, "0123456789") != n) return false;
    unsigned long long value = strtoull(serial, NULL, 10);
    if (!value || value > UINT32_MAX) return false;
    if (!inet_ntop(AF_INET, &ip, row->host, sizeof(row->host))) return false;
    strcpy(row->model, "SOLARMAN");
    strcpy(row->device, serial);
    return true;
}

static size_t logger_search(const search_t *s, app_inverter_discovery_result_t *out, size_t capacity)
{
    int fd = station_socket(&s->net, SOCK_DGRAM);
    if (fd < 0) return 0;
    int yes = 1;
    struct sockaddr_in broadcast = {.sin_family = AF_INET, .sin_port = htons(48899),
                                   .sin_addr.s_addr = htonl(s->net.own | ~s->net.mask)};
    const char query[] = "WIFIKIT-214028-READ";
    size_t count = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == 0 &&
        sendto(fd, query, sizeof(query) - 1, 0, (struct sockaddr *)&broadcast, sizeof(broadcast)) ==
            (ssize_t)sizeof(query) - 1) {
        int64_t until = esp_timer_get_time() + INT64_C(900000);
        for (unsigned attempt = 0; attempt < 32 && count < capacity && socket_ready(fd, false, until); ++attempt) {
            struct sockaddr_in peer;
            socklen_t len = sizeof(peer);
            char packet[112];
            ssize_t n = recvfrom(fd, packet, sizeof(packet), 0, (struct sockaddr *)&peer, &len);
            if (n <= 0 || n >= (ssize_t)sizeof(packet) || memchr(packet, 0, (size_t)n)) continue;
            packet[n] = 0;
            if (!logger_reply(s, packet, ntohl(peer.sin_addr.s_addr), &out[count])) continue;
            size_t i = 0;
            while (i < count && strcmp(out[i].host, out[count].host)) ++i;
            if (i == count) ++count;
        }
    }
    close(fd);
    return count;
}

static void publish(search_t *s, const app_inverter_discovery_result_t *row)
{
    portENTER_CRITICAL(&lock);
    size_t i = 0;
    while (i < s->status.count && (strcmp(s->status.results[i].host, row->host) ||
           strcmp(s->status.results[i].device, row->device))) ++i;
    if (i >= APP_DISCOVERY_MAX_RESULTS) {
        s->status.limited = true;
    } else if (!s->cancel) {
        if (i == s->status.count) ++s->status.count;
        s->status.results[i] = *row;
    }
    portEXIT_CRITICAL(&lock);
}

/* A jelöltre csak a kiválasztott protokollon olvasunk. Felhasználónevet,
 * jelszót, kulcsot vagy tanúsítványt a kereső nem kap. A V5-sorozatszámot
 * kizárólag a saját gyári felderítési válaszában azonosított loggernek adja. */
static void identify(search_t *s, uint32_t ip, const app_inverter_discovery_result_t *hint,
                     app_inverter_discovery_result_t *rows, app_inverter_discovery_state_t *end)
{
    const char *profile = s->status.profile;
    bool modbus = app_inverter_discovery_modbus_supported(profile);
    bool http = app_inverter_discovery_http_supported(profile);
    size_t count = 0;
    int fd = modbus || http ? connect_peer(s, ip, end) : -1;
    if (fd >= 0) {
        int64_t left_ms = (s->deadline - esp_timer_get_time()) / 1000;
        if (left_ms < 1 || !continuing(s, end)) {
            close(fd);
            return;
        }
        memset(rows, 0, sizeof(*rows) * APP_DISCOVERY_MAX_RESULTS);
        uint32_t address = htonl(ip);
        inet_ntop(AF_INET, &address, rows[0].host, sizeof(rows[0].host));
        rows[0].port = s->port;
        rows[0].unit = s->unit;
        uint32_t budget = left_ms < PROBE_MS ? (uint32_t)left_ms : PROBE_MS;
        if (modbus) {
            const char *device = solarman(profile) && hint ? hint->device : s->device;
            if (app_inverter_discovery_modbus(profile, device, s->unit, fd, budget, rows)) {
                count = 1;
                if (!rows[0].device[0] && hint) strcpy(rows[0].device, hint->device);
            }
        } else {
            count = app_inverter_discovery_http(profile, fd, budget, rows, APP_DISCOVERY_MAX_RESULTS);
        }
    }
    if (!continuing(s, end)) return;
    if (!count && hint) {
        rows[0] = *hint;
        count = 1;
    }
    if (count > APP_DISCOVERY_MAX_RESULTS) {
        count = APP_DISCOVERY_MAX_RESULTS;
        portENTER_CRITICAL(&lock);
        s->status.limited = true;
        portEXIT_CRITICAL(&lock);
    }
    for (size_t i = 0; i < count; ++i) {
        uint32_t address = htonl(ip);
        inet_ntop(AF_INET, &address, rows[i].host, sizeof(rows[i].host));
        rows[i].port = s->port;
        rows[i].unit = s->unit;
        publish(s, &rows[i]);
    }
}

static void search_network(search_t *s, scratch_t *buf, app_inverter_discovery_state_t *end)
{
    const char *profile = s->status.profile;
    bool logger = solarman(profile);
    bool fallback = !logger && (app_inverter_discovery_modbus_supported(profile) ||
                                app_inverter_discovery_http_supported(profile));
    if (!continuing(s, end)) return;
    size_t native = logger ? logger_search(s, buf->native, APP_DISCOVERY_MAX_RESULTS + 1) :
                    app_inverter_discovery_native(profile, htonl(s->net.own), buf->native, APP_DISCOVERY_MAX_RESULTS + 1);
    /* A nagy hálózaton a gyártói felderítés az egész helyi szegmenst eléri.
     * Az egyedi címek ellenőrzése a kijelző saját 256 címes részére korlátos. */
    uint32_t mask = s->net.mask | UINT32_C(0xffffff00);
    uint32_t first = s->net.own & mask, last = first | ~mask;
    uint32_t known[APP_DISCOVERY_MAX_RESULTS + 1] = {0};
    bool limited = (fallback && mask != s->net.mask) || native > APP_DISCOVERY_MAX_RESULTS;
    if (native > APP_DISCOVERY_MAX_RESULTS + 1) native = APP_DISCOVERY_MAX_RESULTS + 1;
    unsigned total = 0;
    if (fallback) for (uint32_t ip = first; ip <= last; ++ip) total += eligible(&s->net, ip);
    for (size_t i = 0; i < native; ++i) {
        uint32_t addr;
        if (inet_pton(AF_INET, buf->native[i].host, &addr) != 1 || !eligible(&s->net, ntohl(addr))) continue;
        known[i] = ntohl(addr);
        for (size_t k = 0; k < i; ++k) if (known[i] == known[k]) known[i] = 0;
        if (known[i] && (!fallback || known[i] < first || known[i] > last)) ++total;
    }
    portENTER_CRITICAL(&lock);
    s->status.total = total;
    s->status.limited = limited;
    portEXIT_CRITICAL(&lock);
    for (size_t i = 0; i < native; ++i) {
        if (!known[i]) continue;
        if (!continuing(s, end)) return;
        identify(s, known[i], &buf->native[i], buf->probe, end);
        portENTER_CRITICAL(&lock);
        ++s->status.checked;
        portEXIT_CRITICAL(&lock);
    }
    if (!fallback) return;
    for (uint32_t ip = first; ip <= last; ++ip) {
        if (!eligible(&s->net, ip)) continue;
        size_t i = 0;
        while (i < native && known[i] != ip) ++i;
        if (i < native) continue;
        if (!continuing(s, end)) return;
        identify(s, ip, NULL, buf->probe, end);
        portENTER_CRITICAL(&lock);
        ++s->status.checked;
        portEXIT_CRITICAL(&lock);
    }
}

static void search_task(void *arg)
{
    search_t *s = arg;
    app_inverter_discovery_state_t end = APP_DISCOVERY_FAILED;
    scratch_t *buf = heap_caps_calloc(1, sizeof(*buf), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
        end = APP_DISCOVERY_DONE;
        search_network(s, buf, &end);
        free(buf);
    }
    int64_t expires = esp_timer_get_time() + RESULT_TTL_US;
    ESP_LOGI("inverter_search", "Keresés vége, szabad feladatverem minimuma: %u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&lock);
    s->expires = expires;
    s->status.state = s->cancel ? APP_DISCOVERY_CANCELLED : end;
    s->finished_task = self;
    atomic_store(&cached, true);
    portEXIT_CRITICAL(&lock);
    /* Az IDF WithCaps öntörléshez új belső vermet foglalna. A tick vagy a
     * következő keresés kívülről törli ezt a feladatot, további foglalás nélkül. */
    for (;;) vTaskSuspend(NULL);
}

esp_err_t app_inverter_discovery_start(const char *profile, uint16_t port, uint8_t unit,
                                       const char *device, uint32_t *job)
{
    if (!job || !profile || !port || (device && strlen(device) >= sizeof(((search_t *)0)->device)))
        return ESP_ERR_INVALID_ARG;
    if (!app_inverter_discovery_supported(profile)) return ESP_ERR_NOT_SUPPORTED;
    network_t net;
    if (!network_now(&net)) return ESP_ERR_INVALID_STATE;
    search_t *s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) return ESP_ERR_NO_MEM;
    s->net = net;
    s->port = port;
    s->unit = unit;
    strcpy(s->status.profile, profile);
    /* A régi V5-modul sorozatszáma ne takarja el a kicserélt vagy második adatgyűjtőt. */
    if (device && !strcmp(profile, "solax_modbus")) strcpy(s->device, device);
    s->deadline = esp_timer_get_time() + SEARCH_US;
    s->status.state = APP_DISCOVERY_RUNNING;
    portENTER_CRITICAL(&lock);
    search_t *previous = active;
    if (previous && previous->status.state == APP_DISCOVERY_RUNNING) {
        portEXIT_CRITICAL(&lock);
        free(s);
        return ESP_ERR_INVALID_STATE;
    }
    if (!++sequence) ++sequence;
    *job = sequence;
    s->status.job = sequence;
    active = s;
    atomic_store(&cached, false);
    portEXIT_CRITICAL(&lock);
    if (previous && previous->finished_task) {
        vTaskDeleteWithCaps(previous->finished_task);
        previous->finished_task = NULL;
    }
    if (xTaskCreateWithCaps(search_task, "inverter_search", SEARCH_STACK_BYTES, s, 2, NULL,
                            MALLOC_CAP_RTCRAM) != pdPASS &&
        xTaskCreateWithCaps(search_task, "inverter_search", SEARCH_STACK_BYTES, s, 2, NULL,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        portENTER_CRITICAL(&lock);
        active = previous;
        atomic_store(&cached, previous != NULL);
        portEXIT_CRITICAL(&lock);
        free(s);
        return ESP_ERR_NO_MEM;
    }
    free(previous);
    return ESP_OK;
}

esp_err_t app_inverter_discovery_snapshot(uint32_t job, app_inverter_discovery_status_t *out)
{
    if (!job || !out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&lock);
    bool found = active && active->status.job == job;
    if (found) *out = active->status;
    portEXIT_CRITICAL(&lock);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t app_inverter_discovery_cancel(uint32_t job)
{
    if (!job) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&lock);
    bool found = active && active->status.job == job;
    if (found && active->status.state == APP_DISCOVERY_RUNNING) active->cancel = true;
    portEXIT_CRITICAL(&lock);
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void app_inverter_discovery_expire(void)
{
    if (!atomic_load(&cached)) return;
    int64_t now = esp_timer_get_time();
    search_t *old = NULL;
    TaskHandle_t finished = NULL;
    portENTER_CRITICAL(&lock);
    if (active && active->status.state != APP_DISCOVERY_RUNNING) {
        finished = active->finished_task;
        active->finished_task = NULL;
        if (now >= active->expires) {
            old = active;
            active = NULL;
            atomic_store(&cached, false);
        }
    }
    portEXIT_CRITICAL(&lock);
    if (finished) vTaskDeleteWithCaps(finished);
    free(old);
}
