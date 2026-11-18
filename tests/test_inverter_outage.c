/* A folyamatosan működő Master adatútja egy kiesés és egy csendes DHCP-költözés során.
 * A valódi worker, MAC-azonosítás, tárolás, Modbus-meghajtó és képernyőállapot fut.
 * A tesztóra, a helyi szomszédok és a kernel loopback útvonala ellenőrizhető. */
#include "app_inverter.h"
#include "app_inverter_identity.h"
#include "app_inverter_settings.h"
#include "app_inverter_weather.h"
#include "app_ota.h"
#include "app_stats.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define OLD_IP "192.0.2.4"
#define NEW_IP "192.0.2.6"
#define DEVICE_PORT 1502

typedef enum { ONLINE, UNPLUGGED, REASSIGNED, MOVED, REBOOTED } phase_t;
static phase_t phase;
static const uint8_t inverter_mac[] = {0x02, 0, 0, 0, 0, 1};
static const uint8_t other_mac[] = {0x02, 0, 0, 0, 0, 2};
static const uint8_t gateway_mac[] = {0x02, 0, 0, 0, 0, 3};
static uint32_t discovered, announcement_sequence;
static unsigned successful_old, outage_attempts, foreign_attempts, moved_attempts, found_by_probe;
static unsigned samples;
static int old_port, refused_port, foreign_port, moved_port;
static int64_t now = 1000000;

time_t __wrap_time(time_t *out)
{
    time_t value = 1782010800 + now / 1000000;
    if (out) *out = value;
    return value;
}

int64_t esp_timer_get_time(void) { return now; }

void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps);
    return calloc(count, size);
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *m = malloc(sizeof(*m));
    assert(m && !pthread_mutex_init(m, NULL));
    return m;
}

void vSemaphoreDelete(SemaphoreHandle_t m) { assert(!pthread_mutex_destroy(m)); free(m); }
int xSemaphoreTake(SemaphoreHandle_t m, unsigned t) { (void)t; return !pthread_mutex_lock(m); }
int xSemaphoreGive(SemaphoreHandle_t m) { return !pthread_mutex_unlock(m); }

/* A két valódi firmware-blob tartalma külön folyamatból is visszaolvasható. */
static const char *stage_path(const char *key)
{
    if (!strcmp(key, "profile1")) return "profile1.tmp";
    assert(!strcmp(key, "lan_pin"));
    return "lan_pin.tmp";
}

esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *out)
{
    assert(!strcmp(space, "inverter_v4") && (mode == NVS_READONLY || mode == NVS_READWRITE));
    *out = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h == 1); }

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{
    assert(h == 1);
    FILE *f = fopen(stage_path(key), "wb");
    assert(f && fwrite(data, 1, size, f) == size && !fclose(f));
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *len)
{
    assert(h == 1);
    (void)stage_path(key);
    FILE *f = fopen(key, "rb");
    if (!f) { assert(errno == ENOENT); return ESP_ERR_NVS_NOT_FOUND; }
    *len = fread(data, 1, *len, f);
    assert(!ferror(f) && fgetc(f) == EOF && !fclose(f));
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t h)
{
    assert(h == 1);
    const char *keys[] = {"profile1", "lan_pin"};
    for (unsigned i = 0; i < 2; ++i)
        assert(rename(stage_path(keys[i]), keys[i]) == 0 || errno == ENOENT);
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h; (void)key;
    abort();  /* A profil újrapárosítása nem része a DHCP-költözésnek. */
}

static const uint8_t *neighbor(uint32_t addr)
{
    if (addr == inet_addr(OLD_IP)) return phase <= UNPLUGGED ? inverter_mac : other_mac;
    if (addr == inet_addr(NEW_IP) && phase >= MOVED) return inverter_mac;
    return NULL;
}

void app_inverter_link_idle(void) {}

bool app_inverter_link(uint32_t addr, const uint8_t mac[6], bool probe, app_inverter_link_t *out)
{
    (void)probe;
    *out = (app_inverter_link_t){.local = inet_addr("192.0.2.3"), .gateway = inet_addr("192.0.2.1"),
                                 .mask = inet_addr("255.255.255.248"), .ssid = "test-house"};
    memcpy(out->router_mac, gateway_mac, 6);
    const uint8_t *exact = neighbor(addr);
    if (exact && (addr != inet_addr(NEW_IP) || discovered || phase == REBOOTED))
        memcpy(out->exact_mac, exact, 6);
    if (mac && !memcmp(mac, inverter_mac, 6)) {
        out->announced = discovered;
        out->sequence = announcement_sequence;
    }
    return true;
}

bool app_inverter_link_who_has(const app_inverter_pin_t *pin, uint32_t target)
{
    assert(!memcmp(pin->mac, inverter_mac, 6));
    /* Nincs GARP: kizárólag az új címre küldött, ismert MAC-nek szóló ARP
     * kérdés hozhat a worker számára használható új bizonyítékot. */
    if (target == inet_addr(NEW_IP) && phase == MOVED) {
        discovered = target;
        ++found_by_probe;
        ++announcement_sequence;
    }
    return true;
}

/* A hálózati címet csak a connect határán fordítjuk a teszt LAN-járól
 * loopbackre. A címválasztás, a socket és a Modbus csomag valódi marad. */
int __real_connect(int fd, const struct sockaddr *addr, socklen_t len);
int __wrap_connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    assert(addr->sa_family == AF_INET && len == sizeof(struct sockaddr_in));
    struct sockaddr_in mapped;
    memcpy(&mapped, addr, sizeof(mapped));
    assert(ntohs(mapped.sin_port) == DEVICE_PORT);
    int port;
    if (mapped.sin_addr.s_addr == inet_addr(NEW_IP)) {
        ++moved_attempts;
        assert(phase >= MOVED);
        port = moved_port;
    } else {
        assert(mapped.sin_addr.s_addr == inet_addr(OLD_IP));
        if (phase == ONLINE) { ++successful_old; port = old_port; }
        else if (phase == UNPLUGGED) { ++outage_attempts; port = refused_port; }
        else { ++foreign_attempts; port = foreign_port; }
    }
    mapped.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    mapped.sin_port = htons(port);
    return __real_connect(fd, (const struct sockaddr *)&mapped, sizeof(mapped));
}

void app_stats_break_continuity(void) {}
void app_stats_process_sample(const app_energy_sample_t *s)
{
    assert(s->received_epoch_s == __wrap_time(NULL));
    ++samples;
}
bool app_ota_operation_active(void) { return false; }
void app_inverter_http_reset(void) {}
void app_inverter_victron_close(void) {}
bool app_inverter_http_read(const app_inverter_config_t *c, app_energy_sample_t *s, int64_t *e)
{ (void)c; (void)s; (void)e; abort(); }
bool app_inverter_victron_read(const app_inverter_config_t *c, app_energy_sample_t *s)
{ (void)c; (void)s; abort(); }
void app_inverter_weather_start(void) {}
void app_inverter_weather_apply(app_energy_sample_t *s) { (void)s; }
bool app_inverter_solar(int64_t utc, double lat, double lon, app_solar_position_t *out)
{ (void)utc; (void)lat; (void)lon; (void)out; return false; }

static void cycle(void)
{
    now += 250000;
    app_inverter_tick();
}

static bool searching(void)
{
    app_runtime_state_t s;
    app_state_get_runtime(&s);
    assert(s.wifi_connected && s.time_synced && s.operational);
    return app_state_inverter_searching(&s, now);
}

static void verify_reading(double expected_kw, int64_t expected_time, const char *active)
{
    app_energy_sample_t s;
    assert(app_state_get_sample(&s) && fabs(s.power_pv_kw - expected_kw) < 0.000001);
    assert(s.received_epoch_s == expected_time && s.battery_unavailable);
    cJSON *settings = app_inverter_settings_get();
    assert(settings && !strcmp(cJSON_GetObjectItem(settings, "host")->valuestring, OLD_IP));
    assert(!strcmp(cJSON_GetObjectItem(settings, "activeIpv4")->valuestring, active));
    cJSON_Delete(settings);
}

static void configure(void)
{
    cJSON *form = cJSON_Parse("{\"profile\":\"huawei_modbus\",\"host\":\"" OLD_IP "\","
        "\"port\":1502,\"unit\":7,\"battery\":false,\"latitude\":47.5,\"longitude\":19.0}");
    assert(form && app_inverter_settings_save(form) == ESP_OK);
    cJSON_Delete(form);
}

static void test_outage(void)
{
    assert(!searching());
    app_inverter_tick();
    int64_t first_read = __wrap_time(NULL);
    assert(samples == 1 && successful_old == 1);
    verify_reading(2.5, first_read, OLD_IP);
    assert(!searching());
    phase = UNPLUGGED;
    for (unsigned i = 0; i < 20; ++i) cycle();
    assert(outage_attempts == 1 && !searching());
    phase = REASSIGNED;
    for (unsigned i = 0; i < 19; ++i) { cycle(); assert(!searching()); }
    cycle();
    assert(searching() && samples == 1 && !foreign_attempts);
    verify_reading(2.5, first_read, OLD_IP);
    phase = MOVED;
    /* Az első körben a készülék még ki volt kapcsolva. A következő kört a
     * visszavárakozás után is a Master kezdeményezi, közben a régi mérés megmarad. */
    for (unsigned i = 0; i < 480 && samples == 1; ++i) {
        cycle();
        if (samples == 1) assert(searching());
    }
    assert(found_by_probe && discovered == inet_addr(NEW_IP));
    assert(samples == 2 && moved_attempts == 1 && !foreign_attempts && !searching());
    verify_reading(4.1, __wrap_time(NULL), NEW_IP);
}

int main(int argc, char **argv)
{
    assert(argc == 6);
    old_port = atoi(argv[2]);
    refused_port = atoi(argv[3]);
    foreign_port = atoi(argv[4]);
    moved_port = atoi(argv[5]);
    app_state_init();
    app_state_set_data_mode(APP_DATA_INVERTER);
    app_state_set_wifi_connected(true);
    app_state_set_time_synced(true);
    app_state_set_operational(true);
    if (!strcmp(argv[1], "configure")) configure();
    else if (!strcmp(argv[1], "outage")) test_outage();
    else {
        assert(!strcmp(argv[1], "restart"));
        phase = REBOOTED;
        app_inverter_tick();
        assert(samples == 1 && moved_attempts == 1 && !foreign_attempts && !searching());
        verify_reading(4.1, __wrap_time(NULL), NEW_IP);
    }
    return 0;
}
