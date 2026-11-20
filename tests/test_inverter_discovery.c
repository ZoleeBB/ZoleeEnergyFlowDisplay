/* Szintetikus STA, nettó idő és socketek, a teszt nem érint hálózatot. */
#include "app_inverter_discovery.h"
#include "app_inverter_settings.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "freertos/idf_additions.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

static const char *own = "192.168.8.2", *mask = "255.255.255.248", *gateway = "192.168.8.1";
static bool wifi = true, decline, slow, stale, interrupt, all, v5, mqtt;
static app_data_mode_t mode = APP_DATA_INVERTER;
static int64_t usec;
static unsigned sockets, protocols, connects, packets, allocations, stack;
static unsigned rtc_limit = 8192, internal_limit = 7936, rtc_attempts, internal_attempts, releases;
static void *stack_memory;
static jmp_buf scheduler;
static bool worker_running, ota, ota_during_search;
static uint32_t id, tested[256];
static int live, kind, index_reply;
static const char *wire[5];
static const char *source[5];
static const char *variant = "";
static void (*run_task)(void *);
static void *context;
static app_inverter_discovery_status_t status;
static char current[16];

void app_state_get_runtime(app_runtime_state_t *state)
{
    state->data_recept_mode = mode;
    state->wifi_connected = wifi;
}
bool app_ota_operation_active(void) { return ota; }
struct esp_netif { int index; };
static esp_netif_t sta;
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    assert(!strcmp(key, "WIFI_STA_DEF"));
    return &sta;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t *iface, esp_netif_ip_info_t *ip)
{
    assert(iface == &sta);
    ip->ip.addr = inet_addr(own);
    ip->netmask.addr = inet_addr(mask);
    ip->gw.addr = inet_addr(gateway);
    return ESP_OK;
}
void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps == 3);
    ++allocations;
    return calloc(count, size);
}
int64_t esp_timer_get_time(void) { return usec; }
int xTaskCreateWithCaps(void (*func)(void *), const char *name, unsigned bytes, void *arg,
                        unsigned priority, TaskHandle_t *handle, unsigned caps)
{
    assert(!stack_memory && !handle && priority == 2 && bytes == 8192 && !strcmp(name, "inverter_search"));
    if (caps == MALLOC_CAP_RTCRAM) {
        ++rtc_attempts;
        if (rtc_limit < bytes) return 0;
    } else {
        assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        ++internal_attempts;
        if (internal_limit < bytes) return 0;
    }
    stack_memory = malloc(bytes);
    assert(stack_memory);
    stack = bytes;
    run_task = func;
    context = arg;
    return pdPASS;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    assert(worker_running && stack_memory);
    return stack_memory;
}
void vTaskSuspend(TaskHandle_t self)
{
    assert(!self && worker_running && stack_memory);
    longjmp(scheduler, 1);
}
void vTaskDeleteWithCaps(TaskHandle_t handle)
{
    assert(!worker_running && handle && handle == stack_memory);
    free(stack_memory);
    stack_memory = NULL;
    ++releases;
}
const app_inverter_profile_t *app_inverter_profile(const char *profile)
{
    static const app_inverter_profile_t valid = {0};
    return !strcmp(profile, "unknown") ? NULL : &valid;
}
bool app_inverter_discovery_modbus_supported(const char *profile)
{
    return !strcmp(profile, "huawei_modbus") || !strcmp(profile, "solax_modbus") ||
           !strcmp(profile, "sofar_v5") || !strcmp(profile, "victron_modbus");
}
bool app_inverter_discovery_http_supported(const char *profile) { return !strcmp(profile, "http"); }

size_t app_inverter_discovery_native(const char *profile, uint32_t local, app_inverter_discovery_result_t *out, size_t n)
{
    assert(n >= 4 && local == inet_addr(own));
    if (strcmp(profile, "victron_modbus") && strcmp(profile, "victron_mqtt")) return 0;
    strcpy(out[0].host, mqtt ? "192.168.8.6" : "192.168.99.9");
    strcpy(out[0].model, "Victron GX");
    strcpy(out[0].device, "aabbccddeeff");
    strcpy(out[1].host, out[0].host);
    strcpy(out[2].host, "10.0.0.7");
    strcpy(out[3].host, own);
    return 4;
}

int __wrap_socket(int domain, int type, int protocol)
{
    assert(domain == AF_INET && !protocol && !live);
    ++sockets;
    kind = type;
    live = 7;
    return live;
}
int __wrap_fcntl(int fd, int op, ...)
{
    assert(fd == live && (op == F_GETFL || op == F_SETFL));
    return 0;
}
int __wrap_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    assert(fd == live && len == sizeof(struct sockaddr_in));
    assert(((struct sockaddr_in *)addr)->sin_addr.s_addr == inet_addr(own));
    return 0;
}
int __wrap_close(int fd)
{
    assert(fd == live);
    live = 0;
    return 0;
}
int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    const struct sockaddr_in *peer = (const struct sockaddr_in *)addr;
    assert(fd == live && kind == SOCK_STREAM && len == sizeof(*peer) && connects < 256);
    assert(ntohs(peer->sin_port) == (v5 ? 8899 : 502));
    uint32_t a = peer->sin_addr.s_addr;
    assert(a != inet_addr(own) && a != inet_addr(gateway) &&
           (a & inet_addr(mask)) == (inet_addr(own) & inet_addr(mask)));
    assert(inet_ntop(AF_INET, &a, current, sizeof(current)));
    for (unsigned i = 0; i < connects; ++i) assert(a != tested[i]);
    tested[connects++] = a;
    if (interrupt && connects == 1) assert(app_inverter_discovery_cancel(id) == ESP_OK);
    if (stale && connects == 1) wifi = false;
    if (ota_during_search && connects == 1) ota = true;
    if (all || v5 || a == inet_addr("192.168.8.5") || a == inet_addr("192.168.99.9")) return 0;
    errno = EINPROGRESS;
    return -1;
}
int __wrap_select(int nfds, fd_set *readset, fd_set *writeset, fd_set *err, struct timeval *tv)
{
    assert(!err && live && nfds == live + 1 && tv);
    if (readset && wire[index_reply]) return 1;
    assert(writeset || readset);
    usec += tv->tv_sec * 1000000 + tv->tv_usec;
    return 0;
}
int __wrap_setsockopt(int fd, int layer, int opt, const void *val, socklen_t len)
{
    assert(fd == live && layer == SOL_SOCKET && opt == SO_BROADCAST && len == sizeof(int) && *(const int *)val);
    return 0;
}
ssize_t __wrap_sendto(int fd, const void *msg, size_t len, int flags, const struct sockaddr *addr, socklen_t n)
{
    const struct sockaddr_in *peer = (const struct sockaddr_in *)addr;
    assert(fd == live && kind == SOCK_DGRAM && n == sizeof(*peer) && !flags);
    assert(ntohs(peer->sin_port) == 48899 && peer->sin_addr.s_addr == (inet_addr(own) | ~inet_addr(mask)));
    assert(len == strlen("WIFIKIT-214028-READ") && !memcmp(msg, "WIFIKIT-214028-READ", len));
    ++packets;
    return len;
}
ssize_t __wrap_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *n)
{
    struct sockaddr_in *a = (struct sockaddr_in *)from;
    assert(fd == live && kind == SOCK_DGRAM && !flags && *n == sizeof(*a));
    size_t bytes = strlen(wire[index_reply]);
    assert(bytes < len);
    memcpy(buf, wire[index_reply], bytes);
    a->sin_addr.s_addr = inet_addr(source[index_reply++]);
    return bytes;
}

bool app_inverter_discovery_modbus(const char *profile, const char *device, uint8_t unit, int fd,
                                  uint32_t timeout_ms, app_inverter_discovery_result_t *result)
{
    assert(device && unit == 7 && timeout_ms && timeout_ms <= 1600 && !strcmp(current, result->host));
    assert(!strcmp(profile, "sofar_v5") ? !strcmp(device, "3000000001") : !strcmp(device, variant));
    ++protocols;
    strcpy(result->model, "Ellenőrzött");
    result->verified = true;
    __wrap_close(fd);
    return !decline;
}
size_t app_inverter_discovery_http(const char *profile, int fd, uint32_t timeout_ms,
                                  app_inverter_discovery_result_t *rows, size_t capacity)
{
    assert(!strcmp(profile, "http") && !strcmp(rows[0].host, current) && timeout_ms <= 1600 && capacity >= 2);
    ++protocols;
    __wrap_close(fd);
    strcpy(rows[0].device, "inv-1");
    rows[0].verified = true;
    strcpy(rows[1].device, "inv-2");
    if (slow) usec += 1600000;
    return 2;
}

static void start(const char *profile, const char *device)
{
    protocols = connects = packets = sockets = index_reply = 0;
    assert(app_inverter_discovery_start(profile, v5 ? 8899 : 502, 7, device, &id) == ESP_OK);
    assert(stack == 8192);
    assert(app_inverter_discovery_snapshot(id, &status) == ESP_OK && status.state == APP_DISCOVERY_RUNNING);
}
static void finish(void)
{
    worker_running = true;
    if (!setjmp(scheduler)) {
        run_task(context);
        assert(0 && "a WithCaps worker must suspend at completion");
    }
    worker_running = false;
    assert(!live && stack_memory);
    assert(app_inverter_discovery_snapshot(id, &status) == ESP_OK);
}
int main(void)
{
    assert(app_inverter_discovery_start("unknown", 502, 7, NULL, &id) == ESP_ERR_NOT_SUPPORTED);
    mode = APP_DATA_LOXONE;
    assert(app_inverter_discovery_start("huawei_modbus", 502, 7, NULL, &id) == ESP_ERR_INVALID_STATE && !allocations);
    mode = APP_DATA_INVERTER;
    start("huawei_modbus", "nem-küldhető-azonosító");
    uint32_t first = id, unused;
    assert(app_inverter_discovery_start("huawei_modbus", 502, 7, NULL, &unused) == ESP_ERR_INVALID_STATE);
    assert(app_inverter_discovery_cancel(first + 1) == ESP_ERR_NOT_FOUND);
    finish();
    assert(status.state == APP_DISCOVERY_DONE && connects == 4 && status.checked == 4 && status.total == 4 &&
           !status.limited && protocols == 1 && status.count == 1 && status.results[0].verified);
    assert(!strcmp(status.results[0].host, "192.168.8.5"));
    assert(rtc_attempts == 1 && !internal_attempts && !releases);
    rtc_limit = 7936;
    internal_limit = 8192;
    start("solax_modbus", "X3_G4");
    assert(rtc_attempts == 2 && internal_attempts == 1 && releases == 1);
    variant = "X3_G4";
    assert(app_inverter_discovery_snapshot(first, &status) == ESP_ERR_NOT_FOUND);
    finish();
    variant = "";
    internal_limit = 7936;
    assert(app_inverter_discovery_start("huawei_modbus", 502, 7, NULL, &unused) == ESP_ERR_NO_MEM);
    assert(app_inverter_discovery_snapshot(id, &status) == ESP_OK);
    assert(rtc_attempts == 3 && internal_attempts == 2 && releases == 2 && !stack_memory);
    rtc_limit = 8192;

    ota_during_search = true;
    start("http", "");
    finish();
    assert(status.state == APP_DISCOVERY_FAILED && !status.count && ota);
    app_inverter_discovery_expire();
    app_inverter_discovery_expire();
    assert(releases == 3 && !stack_memory && app_inverter_discovery_snapshot(id, &status) == ESP_OK);
    assert(app_inverter_discovery_start("http", 502, 7, NULL, &unused) == ESP_ERR_INVALID_STATE);
    ota_during_search = ota = false;

    stale = true;
    start("http", "");
    finish();
    assert(status.state == APP_DISCOVERY_FAILED && !status.count);
    wifi = true;
    stale = false;
    interrupt = true;
    start("http", "");
    finish();
    assert(status.state == APP_DISCOVERY_CANCELLED && !status.count && connects == 1);
    interrupt = false;
    start("http", "");
    finish();
    assert(status.state == APP_DISCOVERY_DONE && status.count == 2 && !strcmp(status.results[1].device, "inv-2"));
    assert(!strcmp(status.results[1].host, status.results[0].host));

    mask = "255.255.0.0";
    start("victron_modbus", "");
    finish();
    assert(connects == 255 && status.limited && status.checked == status.total && status.count == 2);
    mqtt = true;
    start("victron_mqtt", "");
    finish();
    assert(!connects && !status.limited && status.total == 1 && status.checked == 1 && status.count == 1);
    assert(!status.results[0].verified && !strcmp(status.results[0].device, "aabbccddeeff"));
    all = slow = true;
    start("http", "");
    finish();
    assert(status.state == APP_DISCOVERY_DONE && status.limited && status.count == 8 &&
           status.checked < status.total && connects < 60);
    all = slow = false;

    mask = "255.255.255.248";
    v5 = true;
    wire[0] = "192.168.8.6,012345ABCDEF,3000000001"; source[0] = "192.168.8.5";
    wire[1] = "192.168.8.6,012345ABCDEF,8000000000"; source[1] = "192.168.8.6";
    wire[2] = "192.168.8.6,012345ABCDEF,3000000001"; source[2] = "192.168.8.6";
    wire[3] = wire[2]; source[3] = source[2];
    start("sofar_v5", "");
    finish();
    assert(packets == 1 && connects == 1 && protocols == 1 && status.count == 1 && status.results[0].verified);
    assert(!strcmp(status.results[0].device, "3000000001"));
    decline = true;
    start("sofar_v5", "3000000001");
    finish();
    assert(status.count == 1 && !status.results[0].verified && !strcmp(status.results[0].model, "SOLARMAN"));
    start("sofar_v5", "3000000002");
    finish();
    assert(connects == 1 && protocols == 1 && status.count == 1 && !strcmp(status.results[0].device, "3000000001"));
    usec += 59999999;
    app_inverter_discovery_expire();
    assert(app_inverter_discovery_snapshot(id, &status) == ESP_OK);
    ++usec;
    app_inverter_discovery_expire();
    assert(app_inverter_discovery_snapshot(id, &status) == ESP_ERR_NOT_FOUND);
    assert(!stack_memory && releases == rtc_attempts - 1);
    puts("Inverterkeresés állapotgép, címkorlátok, gyártói találatok és takarítás rendben.");
    return 0;
}
