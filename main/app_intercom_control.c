/* A Master az egyetlen engedélyező. A Slave legfeljebb 3,5 másodperces
 * visszaigazolás nélkül már leállítja a mikrofont, a Master 15 másodpercig
 * fenntartja a foglalást. Új Master-boot ugyanennyi karanténnal indul. */
#include "app_intercom_control.h"
#include "app_intercom_audio.h"
#include "app_ringtones.h"
#include "app_wifi.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static portMUX_TYPE s_control_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

#ifndef APP_INTERCOM_SLAVE
static uint32_t s_boot;
static app_intercom_arbiter_t s_arbiter;
static unsigned s_day = 85, s_night = 20;
static char s_media_key[65];
static unsigned s_gate_revision;
static bool s_gate_seen;
static uint8_t s_gate_counter;
static void init(void)
{
    portENTER_CRITICAL(&s_control_lock);
    if (!s_boot) {
        s_boot = esp_random();
        if (!s_boot) s_boot = 1;
        s_arbiter.quarantine = esp_timer_get_time() + APP_INTERCOM_LEASE_US;
        for (unsigned i = 0; i < 8; i++) snprintf(s_media_key + i * 8, 9, "%08lx", (unsigned long)esp_random());
    }
    portEXIT_CRITICAL(&s_control_lock);
}

void app_intercom_control_init(const app_config_t *config)
{
    s_day = config->display.day_brightness;
    s_night = config->display.night_brightness;
    init();
}

static bool state(app_intercom_snapshot_t *snapshot)
{
    init();
    return app_loxone_intercom_snapshot(snapshot);
}

void app_intercom_control_tick(void)
{
    app_intercom_snapshot_t snapshot;
    if (!state(&snapshot)) return;
    portENTER_CRITICAL(&s_control_lock);
    app_intercom_arbiter_tick(&s_arbiter, snapshot.revision, esp_timer_get_time());
    portEXIT_CRITICAL(&s_control_lock);
}

bool app_intercom_control_acquire(unsigned revision)
{
    app_intercom_snapshot_t snapshot;
    if (!state(&snapshot) || snapshot.revision != revision) return false;
    portENTER_CRITICAL(&s_control_lock);
    app_intercom_arbiter_tick(&s_arbiter, revision, esp_timer_get_time());
    bool ok = app_intercom_arbiter_command(&s_arbiter, 1, APP_INTERCOM_ACQUIRE, 0,
        snapshot.ringing && snapshot.connected, esp_timer_get_time());
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

bool app_intercom_control_allowed(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool ok = s_arbiter.owner == 1 && s_arbiter.revision == revision;
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

void app_intercom_control_release(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    if (s_arbiter.owner == 1) s_arbiter.owner = 0;
    (void)revision;
    portEXIT_CRITICAL(&s_control_lock);
}

bool app_intercom_control_held(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool held = s_arbiter.owner && s_arbiter.revision == revision &&
        (s_arbiter.owner == 1 || esp_timer_get_time() < s_arbiter.deadline);
    portEXIT_CRITICAL(&s_control_lock);
    return held;
}

bool app_intercom_control_other(unsigned revision)
{
    init();
    portENTER_CRITICAL(&s_control_lock);
    bool held = esp_timer_get_time() < s_arbiter.quarantine ||
        (s_arbiter.owner > 1 && s_arbiter.revision == revision && esp_timer_get_time() < s_arbiter.deadline);
    portEXIT_CRITICAL(&s_control_lock);
    return held;
}

bool app_intercom_control_silenced(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool silenced = s_arbiter.revision == revision && s_arbiter.silenced;
    portEXIT_CRITICAL(&s_control_lock);
    return silenced;
}

void app_intercom_control_gate(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    app_intercom_arbiter_tick(&s_arbiter, revision, esp_timer_get_time());
    if (s_arbiter.revision == revision) {
        s_arbiter.silenced = true;
        s_gate_revision = revision;
        s_gate_seen = true;
        ++s_gate_counter;
    }
    portEXIT_CRITICAL(&s_control_lock);
}

void app_intercom_control_status(uint32_t peer, uint8_t out[APP_INTERCOM_CONTROL_SIZE])
{
    app_intercom_snapshot_t snapshot = {0};
    bool ready = state(&snapshot);
    int64_t now = esp_timer_get_time();
    memset(out, 0, APP_INTERCOM_CONTROL_SIZE);
    out[0] = 1;
    portENTER_CRITICAL(&s_control_lock);
    app_intercom_arbiter_tick(&s_arbiter, snapshot.revision, now);
    uint32_t owner = s_arbiter.owner;
    out[1] = (ready && snapshot.config.enabled ? APP_INTERCOM_ENABLED : 0) |
        (snapshot.ringing ? APP_INTERCOM_RINGING : 0) |
        (snapshot.connected ? APP_INTERCOM_CONNECTED : 0) |
        (snapshot.gate_supported ? APP_INTERCOM_GATE_READY : 0) |
        (snapshot.command_pending ? APP_INTERCOM_PENDING : 0) |
        (owner || now < s_arbiter.quarantine ? APP_INTERCOM_HELD : 0) |
        (owner == peer ? APP_INTERCOM_OWNED : 0) |
        (s_arbiter.silenced ? APP_INTERCOM_SILENCED : 0);
    out[2] = s_gate_seen && s_gate_revision == snapshot.revision ? s_gate_counter : 0;
    out[3] = owner != 0;
    put32(out + 4, s_boot);
    put32(out + 8, snapshot.revision);
    put32(out + 12, s_arbiter.grant);
    portEXIT_CRITICAL(&s_control_lock);
}

bool app_intercom_control_command(uint32_t peer, const uint8_t *data, size_t size)
{
    if (size != APP_INTERCOM_CONTROL_SIZE || data[0] != 1 || peer <= 1) return false;
    app_intercom_snapshot_t snapshot;
    if (!state(&snapshot) || get32(data + 4) != s_boot || get32(data + 8) != snapshot.revision) return false;
    uint8_t operation = data[1];
    if (operation == APP_INTERCOM_GATE) {
        if (!snapshot.connected || (!snapshot.ringing && !snapshot.audio_active)) return false;
        return app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE) == ESP_OK;
    }
    portENTER_CRITICAL(&s_control_lock);
    app_intercom_arbiter_tick(&s_arbiter, snapshot.revision, esp_timer_get_time());
    bool ok = app_intercom_arbiter_command(&s_arbiter, peer, operation, get32(data + 12),
        snapshot.config.enabled && snapshot.connected && snapshot.ringing, esp_timer_get_time());
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

bool app_intercom_control_profile(unsigned part, uint8_t *data)
{
    app_intercom_snapshot_t snapshot;
    if (part > 1 || !state(&snapshot)) return false;
    memset(data, 0, part ? APP_INTERCOM_EXTRAS_SIZE : APP_INTERCOM_PROFILE_SIZE);
    data[0] = 1;
    put32(data + 4, s_boot);
    put32(data + 8, snapshot.revision);
    if (!part) {
        snprintf((char *)data + 12, 257, "%s", snapshot.config.stream_url);
        char user[96] = {0}, password[LX_PASSWORD_MAX] = {0};
        if (snapshot.config.enabled && !app_loxone_intercom_credentials(snapshot.revision, user, password)) return false;
        if (snapshot.config.enabled) {
            memcpy(data + 269, user, sizeof(user));
            memcpy(data + 365, password, sizeof(password));
            memset(password, 0, sizeof(password));
        }
        data[493] = snapshot.config.speaker_volume;
        data[494] = snapshot.config.microphone_gain;
        data[495] = snapshot.config.ringtone_repeat;
        data[496] = snapshot.config.enabled;
    } else {
        data[12] = s_day;
        data[13] = s_night;
        snprintf((char *)data + 16, 81, "%s", snapshot.config.gate.name);
        snprintf((char *)data + 97, 65, "%s", snapshot.config.ringtone);
        app_wifi_get_ip((char *)data + 162, 16);
        memcpy(data + 178, s_media_key, 65);
    }
    return true;
}

bool app_intercom_control_media_key(const char *key)
{
    init();
    if (!key || strlen(key) != 64) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 64; i++) difference |= (unsigned char)key[i] ^ (unsigned char)s_media_key[i];
    return difference == 0;
}

uint8_t *app_intercom_control_ringtone(const char *name, size_t *size)
{
    return app_ringtones_load(name, size);
}
#else
/* A Slave profilja csak RAM-ban él. Sem a Master eszközfüggő SD-titkosítása,
 * sem Miniserver-jelszó nem kerül át a másik készülékre. */
static app_intercom_snapshot_t s_remote;
static uint32_t s_remote_boot, s_grant;
static int64_t s_received;
static uint8_t s_flags, s_operation, s_inflight;
static bool s_want, s_profile_ready, s_remote_active;
static uint8_t s_last_gate;
static uint32_t s_profile_boot, s_profile_revision;
static char s_master_ip[16], s_key[65];
static unsigned s_day = 85, s_night = 20;

void app_intercom_control_init(const app_config_t *config)
{
    s_day = config->display.day_brightness;
    s_night = config->display.night_brightness;
}

static bool fresh(void)
{
    int64_t now = esp_timer_get_time();
    return s_received && now >= s_received && now - s_received < APP_INTERCOM_FRESH_US;
}

bool app_intercom_control_acquire(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool ok = fresh() && s_profile_ready && s_remote.revision == revision &&
              (s_flags & (APP_INTERCOM_RINGING | APP_INTERCOM_CONNECTED | APP_INTERCOM_HELD)) ==
                  (APP_INTERCOM_RINGING | APP_INTERCOM_CONNECTED);
    if (ok) { s_want = true; s_operation = APP_INTERCOM_ACQUIRE; }
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

bool app_intercom_control_allowed(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool ok = fresh() && s_want && s_profile_ready && s_remote.revision == revision &&
              (s_flags & APP_INTERCOM_OWNED) && s_grant;
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

void app_intercom_control_release(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    if (s_remote.revision == revision) {
        s_want = false;
        s_operation = APP_INTERCOM_RELEASE;
    }
    portEXIT_CRITICAL(&s_control_lock);
}

bool app_intercom_control_held(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool held = fresh() && s_remote.revision == revision && (s_flags & APP_INTERCOM_HELD);
    portEXIT_CRITICAL(&s_control_lock);
    return held;
}

bool app_intercom_control_other(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool held = !fresh() || !s_profile_ready || s_remote.revision != revision ||
                ((s_flags & APP_INTERCOM_HELD) && !(s_flags & APP_INTERCOM_OWNED));
    portEXIT_CRITICAL(&s_control_lock);
    return held;
}

bool app_intercom_control_silenced(unsigned revision)
{
    portENTER_CRITICAL(&s_control_lock);
    bool silenced = !fresh() || s_remote.revision != revision || (s_flags & APP_INTERCOM_SILENCED) || s_want;
    portEXIT_CRITICAL(&s_control_lock);
    return silenced;
}

void app_intercom_control_gate(unsigned revision) { (void)revision; }
void app_intercom_control_tick(void) {}

void app_intercom_control_disconnected(void)
{
    portENTER_CRITICAL(&s_control_lock);
    s_received = 0;
    s_want = false;
    s_operation = s_inflight = 0;
    s_flags = 0;
    s_profile_ready = false;
    s_profile_boot = 0;
    memset(s_remote.config.video_password, 0, sizeof(s_remote.config.video_password));
    s_key[0] = 0;
    portEXIT_CRITICAL(&s_control_lock);
}

bool app_intercom_control_receive(const uint8_t *data, size_t size)
{
    if (size != APP_INTERCOM_CONTROL_SIZE || data[0] != 1 || !get32(data + 4)) return false;
    portENTER_CRITICAL(&s_control_lock);
    uint32_t boot = get32(data + 4), revision = get32(data + 8);
    if (boot != s_remote_boot || revision != s_remote.revision) {
        s_profile_ready = false;
        s_want = false;
        s_operation = 0;
        memset(s_remote.config.video_password, 0, sizeof(s_remote.config.video_password));
    }
    s_remote_boot = boot;
    s_remote.revision = revision;
    bool gate_opened = data[2] != s_last_gate;
    s_last_gate = data[2];
    s_remote_active = data[3] != 0;
    s_flags = data[1];
    s_grant = get32(data + 12);
    s_received = esp_timer_get_time();
    s_profile_ready = s_profile_boot == boot && s_profile_revision == revision;
    portEXIT_CRITICAL(&s_control_lock);
    if (gate_opened) app_intercom_audio_gate_opened(revision);
    return true;
}

bool app_intercom_control_receive_profile(const uint8_t *p, const uint8_t *x)
{
    if (p[0] != 1 || x[0] != 1 || memcmp(p + 4, x + 4, 8) ||
        !memchr(p + 12, 0, 257) || !memchr(p + 269, 0, 96) || !memchr(p + 365, 0, 128) ||
        !memchr(x + 16, 0, 81) || !memchr(x + 97, 0, 65) || !memchr(x + 162, 0, 16) ||
        !memchr(x + 178, 0, 65) || p[493] > 100 || p[494] > 100 || p[495] > 1 || p[496] > 1 ||
        x[12] > 100 || x[13] > 100) return false;
    portENTER_CRITICAL(&s_control_lock);
    bool ok = get32(p + 4) == s_remote_boot && get32(p + 8) == s_remote.revision;
    if (ok) {
        lx_intercom_config_t *config = &s_remote.config;
        memset(config, 0, sizeof(*config));
        memcpy(config->stream_url, p + 12, 257);
        memcpy(config->video_username, p + 269, 96);
        memcpy(config->video_password, p + 365, 128);
        config->speaker_volume = p[493];
        config->microphone_gain = p[494];
        config->ringtone_repeat = p[495];
        config->enabled = p[496];
        memcpy(config->gate.name, x + 16, 81);
        /* A parancsot a Master oldja fel; a Slave nem kap Loxone UUID-t. */
        if (config->gate.name[0]) strcpy(config->gate.control_uuid, "master");
        memcpy(config->ringtone, x + 97, 65);
        memcpy(s_master_ip, x + 162, 16);
        memcpy(s_key, x + 178, 65);
        s_day = x[12]; s_night = x[13];
        s_profile_boot = s_remote_boot;
        s_profile_revision = s_remote.revision;
        s_profile_ready = true;
    }
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

bool app_intercom_control_outgoing(uint8_t out[APP_INTERCOM_CONTROL_SIZE])
{
    bool busy = app_intercom_audio_busy();
    portENTER_CRITICAL(&s_control_lock);
    if (!s_want && (s_flags & APP_INTERCOM_OWNED) && !busy) s_operation = APP_INTERCOM_RELEASE;
    uint8_t operation = s_operation ? s_operation : s_want && (s_flags & APP_INTERCOM_OWNED) ? APP_INTERCOM_KEEP : 0;
    bool ok = fresh() && s_profile_ready && operation;
    if (ok) {
        memset(out, 0, APP_INTERCOM_CONTROL_SIZE);
        out[0] = 1; out[1] = operation;
        put32(out + 4, s_remote_boot);
        put32(out + 8, s_remote.revision);
        put32(out + 12, s_grant);
        s_inflight = operation;
    }
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

void app_intercom_control_sent(bool success)
{
    portENTER_CRITICAL(&s_control_lock);
    if (s_operation == s_inflight) s_operation = 0;
    if (!success && s_inflight == APP_INTERCOM_ACQUIRE) s_want = false;
    s_inflight = 0;
    portEXIT_CRITICAL(&s_control_lock);
}

bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_control_lock);
    *out = s_remote;
    memset(out->config.video_password, 0, sizeof(out->config.video_password));
    bool ready = fresh() && s_profile_ready && (s_flags & APP_INTERCOM_ENABLED);
    out->ringing = ready && (s_flags & APP_INTERCOM_RINGING);
    out->connected = ready && (s_flags & APP_INTERCOM_CONNECTED);
    out->audio_active = ready && s_remote_active;
    out->audio_elsewhere = (s_flags & APP_INTERCOM_HELD) && !(s_flags & APP_INTERCOM_OWNED);
    out->ringtone_silenced = (s_flags & APP_INTERCOM_SILENCED) || s_want;
    out->gate_supported = (s_flags & APP_INTERCOM_GATE_READY) != 0;
    out->command_pending = (s_flags & APP_INTERCOM_PENDING) != 0;
    portEXIT_CRITICAL(&s_control_lock);
    return ready;
}

bool app_loxone_intercom_credentials(unsigned revision, char user[96], char password[LX_PASSWORD_MAX])
{
    portENTER_CRITICAL(&s_control_lock);
    bool ok = fresh() && s_profile_ready && s_remote.revision == revision;
    if (ok) {
        memcpy(user, s_remote.config.video_username, 96);
        memcpy(password, s_remote.config.video_password, LX_PASSWORD_MAX);
    }
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}

bool app_loxone_intercom_call_permitted(unsigned revision, bool active)
{
    portENTER_CRITICAL(&s_control_lock);
    bool ok = fresh() && s_profile_ready && s_remote.revision == revision &&
        (s_flags & APP_INTERCOM_CONNECTED) && ((s_flags & APP_INTERCOM_RINGING) || active);
    portEXIT_CRITICAL(&s_control_lock);
    return ok;
}
bool app_loxone_intercom_test_permitted(unsigned revision) { (void)revision; return false; }

esp_err_t app_loxone_home_command(const char *id, lx_home_command_t action)
{
    if (!id || strcmp(id, LX_INTERCOM_GATE_ID) || action != LX_HOME_COMMAND_PULSE) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_control_lock);
    bool ok = fresh() && s_profile_ready && !s_operation &&
        (s_flags & (APP_INTERCOM_GATE_READY | APP_INTERCOM_CONNECTED)) == (APP_INTERCOM_GATE_READY | APP_INTERCOM_CONNECTED) &&
        (s_flags & (APP_INTERCOM_RINGING | APP_INTERCOM_HELD));
    if (ok) s_operation = APP_INTERCOM_GATE;
    portEXIT_CRITICAL(&s_control_lock);
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void app_intercom_control_brightness(unsigned *day, unsigned *night)
{
    portENTER_CRITICAL(&s_control_lock);
    *day = s_day; *night = s_night;
    portEXIT_CRITICAL(&s_control_lock);
}

uint8_t *app_intercom_control_ringtone(const char *name, size_t *size)
{
    if (!app_ringtone_name(name)) return NULL;
    char ip[16], key[65];
    portENTER_CRITICAL(&s_control_lock);
    bool ready = fresh() && s_profile_ready;
    memcpy(ip, s_master_ip, sizeof(ip)); memcpy(key, s_key, sizeof(key));
    portEXIT_CRITICAL(&s_control_lock);
    if (!ready || !ip[0] || !key[0]) return NULL;
    char url[192];
    snprintf(url, sizeof(url), "http://%s/api/intercom/ringtone?name=%s", ip, name);
    esp_http_client_config_t config = {.url = url, .timeout_ms = 1500, .disable_auto_redirect = true};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;
    esp_http_client_set_header(client, "X-Zolee-Media", key);
    uint8_t *data = NULL;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        int64_t n = esp_http_client_fetch_headers(client);
        if (n >= 44 && n <= APP_RINGTONE_MAX && esp_http_client_get_status_code(client) == 200) {
            data = malloc((size_t)n);
            size_t position = 0;
            int64_t deadline = esp_timer_get_time() + 3000000;
            while (data && position < (size_t)n && esp_timer_get_time() < deadline) {
                int count = esp_http_client_read(client, (char *)data + position, (int)(n - position));
                if (count <= 0) break;
                position += count;
            }
            size_t offset, bytes;
            if (data && (position != (size_t)n || !app_ringtone_wav(data, position, &offset, &bytes))) {
                free(data); data = NULL;
            }
            if (data) *size = position;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    memset(key, 0, sizeof(key));
    return data;
}
#endif
