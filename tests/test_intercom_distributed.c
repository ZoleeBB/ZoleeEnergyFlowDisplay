/* A tényleges központi kapu és Slave fogadó tesztje; csak az óra, HTTP és
 * Loxone-pillanatkép helyettesített. Párhuzamos hívásindítás valódi pthread-del. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/app_intercom_control.c"
static int64_t test_now = 1;
int64_t esp_timer_get_time(void)
{
    return test_now;
}
uint32_t esp_random(void)
{
    static uint32_t value = 41;
    return ++value;
}
static unsigned gates;
bool app_intercom_audio_busy(void)
{
    return false;
}
void app_intercom_audio_gate_opened(unsigned revision)
{
    assert(revision == 7);
    gates++;
}

#ifndef APP_INTERCOM_SLAVE
static app_intercom_snapshot_t fixture;
static bool change_after_snapshot;
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t* out)
{
    *out = fixture;
    if (change_after_snapshot) { fixture.revision++; change_after_snapshot = false; }
    return true;
}
bool app_loxone_intercom_credentials(unsigned revision, char user[96], char password[LX_PASSWORD_MAX])
{
    assert(revision == fixture.revision);
    strcpy(user, "admin");
    strcpy(password, "camera-secret");
    return true;
}
esp_err_t app_loxone_home_command(const char* id, lx_home_command_t action)
{
    assert(!strcmp(id, LX_INTERCOM_GATE_ID) && action == LX_HOME_COMMAND_PULSE);
    gates++;
    app_intercom_control_gate(fixture.revision);
    return ESP_OK;
}
esp_err_t app_loxone_intercom_gate_request(unsigned revision)
{
    if (revision != fixture.revision) return ESP_ERR_INVALID_STATE;
    return app_loxone_home_command(LX_INTERCOM_GATE_ID, LX_HOME_COMMAND_PULSE);
}
esp_err_t app_wifi_get_ip(char* out, size_t n)
{
    snprintf(out, n, "192.168.1.10");
    return ESP_OK;
}
uint8_t* app_ringtones_load_ex(const char* name, size_t* size, app_ringtone_load_status_t* status)
{
    (void)name;
    (void)size;
    *status = APP_RINGTONE_LOAD_MISSING;
    return NULL;
}
static uint8_t command[20];
static pthread_barrier_t start;
static bool granted[2];
static void* acquire(void* argument)
{
    unsigned index = (unsigned)(uintptr_t)argument;
    pthread_barrier_wait(&start);
    granted[index] = app_intercom_control_command(2 + index, command, sizeof(command));
    return NULL;
}
int main(void)
{
    fixture.revision = 7;
    fixture.config.enabled = true;
    fixture.ringing = fixture.connected = fixture.gate_supported = true;
    strcpy(fixture.config.stream_url, "http://192.168.1.20/mjpg/video.mjpg");
    strcpy(fixture.config.gate.name, "Kapunyitás");
    fixture.config.speaker_volume = 55;
    fixture.config.microphone_gain = 80;
    app_config_t config = { 0 };
    config.display.day_brightness = 82;
    config.display.night_brightness = 17;
    app_intercom_control_init(&config);
    uint8_t status[20], profile[512], extras[256];
    app_intercom_control_status(2, status);
    assert(status[1] & APP_INTERCOM_HELD);
    assert(!status[3] && !app_intercom_control_acquire(7));
    assert(app_intercom_control_profile(0, profile) && app_intercom_control_profile(1, extras));
    assert(!strcmp((char*)profile + 365, "camera-secret"));
    for (unsigned i = 365 + strlen("camera-secret") + 1; i < 493; i++)
        assert(!profile[i]);
    assert(extras[12] == 82 && extras[13] == 17);
    assert(app_intercom_control_media_key((char*)extras + 178));
    assert(!app_intercom_control_media_key("invalid"));
    memcpy(command, status, 20);
    command[1] = APP_INTERCOM_ACQUIRE;
    test_now += APP_INTERCOM_LEASE_US + 1;
    pthread_barrier_init(&start, NULL, 2);
    pthread_t threads[2];
    pthread_create(&threads[0], NULL, acquire, (void*)0);
    pthread_create(&threads[1], NULL, acquire, (void*)1);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    assert(granted[0] != granted[1]);
    uint32_t owner = granted[0] ? 2 : 3;
    assert(!app_intercom_control_acquire(7));
    app_intercom_control_status(owner, status);
    assert(status[1] & APP_INTERCOM_OWNED);
    assert(app_intercom_control_silenced(7) && app_intercom_control_held(7));
    memcpy(command, status, 20);
    command[1] = APP_INTERCOM_RELEASE;
    command[12] ^= 1;
    assert(!app_intercom_control_command(owner, command, 20));
    command[12] ^= 1;
    assert(!app_intercom_control_command(owner == 2 ? 3 : 2, command, 20));
    assert(app_intercom_control_command(owner, command, 20));
    assert(app_intercom_control_acquire(7) && app_intercom_control_allowed(7));
    assert(!app_intercom_control_command(owner, command, 20));
    fixture.revision = 8;
    app_intercom_control_tick();
    assert(!app_intercom_control_allowed(7));
    app_intercom_control_release(7);
    assert(!s_arbiter.owner);
    fixture.revision = 7;
    app_intercom_control_status(owner, status);
    memcpy(command, status, 20);
    command[1] = APP_INTERCOM_GATE;
    change_after_snapshot = true;
    assert(!app_intercom_control_command(owner, command, 20) && !gates);
    fixture.revision = 7;
    assert(
        app_intercom_control_command(owner, command, 20) && gates == 1 && app_intercom_control_silenced(7));
    command[4] ^= 1;
    assert(!app_intercom_control_command(owner, command, 20));
    command[4] ^= 1;
    command[1] = APP_INTERCOM_ACQUIRE;
    assert(app_intercom_control_command(owner, command, 20));
    test_now += APP_INTERCOM_LEASE_US + 1;
    app_intercom_control_tick();
    assert(!app_intercom_control_held(7) && app_intercom_control_acquire(7));
    puts("PASS: real Master arbiter, simultaneous peers, pre-SIP grant, owner/token/revision/boot rejection, "
         "bounded lease, restart quarantine, gate suppression, secret profile and brightness");
    return 0;
}
#else
static uint8_t wave_data[684];
static unsigned http_reads, http_cleanups;
static size_t http_position;
static bool cancel_download;
void app_intercom_audio_snapshot(app_intercom_audio_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->state = cancel_download && http_reads ? APP_INTERCOM_AUDIO_STOPPING : APP_INTERCOM_AUDIO_RINGING;
}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->timeout_ms == 500 && config->disable_auto_redirect);
    assert(!strcmp(config->url, "http://192.168.1.10/api/intercom/ringtone?name=doorbell.wav"));
    http_position = 0; http_reads = 0;
    return (void *)1;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *name, const char *value)
{
    assert(client == (void *)1 && !strcmp(name, "X-Zolee-Media") && strlen(value) == 64);
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int size) { assert(client && !size); return ESP_OK; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { assert(client); return sizeof(wave_data); }
int esp_http_client_get_status_code(esp_http_client_handle_t client) { assert(client); return 200; }
int esp_http_client_read(esp_http_client_handle_t client, char *out, int size)
{
    assert(client && size > 0);
    size_t count = sizeof(wave_data) - http_position;
    if (count > (size_t)size) count = (size_t)size;
    if (count > 100) count = 100;
    memcpy(out, wave_data + http_position, count);
    http_position += count; http_reads++;
    return (int)count;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { assert(client); return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) { assert(client); http_cleanups++; return ESP_OK; }

int main(void)
{
    app_config_t config = { 0 };
    app_intercom_control_init(&config);
    uint8_t status[20] = { 1 }, profile[512] = { 1 }, extras[256] = { 1 }, command[20];
    put32(status + 4, 11);
    put32(status + 8, 7);
    status[1]
        = APP_INTERCOM_ENABLED | APP_INTERCOM_RINGING | APP_INTERCOM_CONNECTED | APP_INTERCOM_GATE_READY;
    assert(app_intercom_control_receive(status, 20));
    app_intercom_snapshot_t snapshot;
    assert(!app_loxone_intercom_snapshot(&snapshot));
    memcpy(profile + 4, status + 4, 8);
    memcpy(extras + 4, status + 4, 8);
    strcpy((char*)profile + 12, "http://192.168.1.20/mjpg/video.mjpg");
    strcpy((char*)profile + 269, "admin");
    strcpy((char*)profile + 365, "camera-secret");
    profile[493] = 55;
    profile[494] = 80;
    profile[495] = profile[496] = 1;
    strcpy((char*)extras + 16, "Kapunyitás");
    strcpy((char*)extras + 97, "doorbell.wav");
    strcpy((char *)extras + 162, "192.168.1.10");
    memset(extras + 178, 'f', 64);
    extras[12] = 82;
    extras[13] = 17;
    assert(app_intercom_control_receive_profile(profile, extras));
    assert(app_loxone_intercom_snapshot(&snapshot) && snapshot.ringing && !snapshot.config.video_password[0]);
    assert(!app_intercom_control_allowed(7));
    memcpy(wave_data, "RIFF", 4); put32(wave_data + 4, sizeof(wave_data) - 8);
    memcpy(wave_data + 8, "WAVEfmt ", 8); put32(wave_data + 16, 16);
    wave_data[20] = 1; wave_data[22] = 1; put32(wave_data + 24, 16000); put32(wave_data + 28, 32000);
    wave_data[32] = 2; wave_data[34] = 16; memcpy(wave_data + 36, "data", 4); put32(wave_data + 40, 640);
    size_t downloaded_size;
    app_ringtone_load_status_t load_status;
    cancel_download = true;
    assert(!app_intercom_control_ringtone("doorbell.wav", &downloaded_size, &load_status));
    assert(http_reads == 1 && http_cleanups == 1);
    cancel_download = false;
    uint8_t *downloaded = app_intercom_control_ringtone("doorbell.wav", &downloaded_size, &load_status);
    assert(downloaded && downloaded_size == sizeof(wave_data) && !memcmp(downloaded, wave_data, downloaded_size));
    assert(http_cleanups == 2); free(downloaded);
    assert(app_intercom_control_acquire(7));
    assert(!app_intercom_control_acquire(7));
    assert(app_intercom_control_outgoing(command) && command[1] == APP_INTERCOM_ACQUIRE);
    app_intercom_control_sent(true);
    assert(!app_intercom_control_allowed(7));
    put32(status + 12, 5);
    status[1] |= APP_INTERCOM_HELD | APP_INTERCOM_OWNED;
    status[3] = 1;
    assert(app_intercom_control_receive(status, 20) && app_intercom_control_allowed(7));
    assert(app_intercom_control_outgoing(command) && command[1] == APP_INTERCOM_KEEP);
    app_intercom_control_sent(true);
    status[2] = 1;
    assert(app_intercom_control_receive(status, 20) && gates == 1);
    assert(app_intercom_control_receive(status, 20) && gates == 1);
    unsigned day, night;
    app_intercom_control_brightness(&day, &night);
    assert(day == 82 && night == 17);
    app_intercom_control_release(7);
    assert(app_intercom_control_outgoing(command) && command[1] == APP_INTERCOM_RELEASE
        && get32(command + 12) == 5);
    app_intercom_control_sent(true);
    test_now += APP_INTERCOM_FRESH_US + 1;
    assert(!app_intercom_control_allowed(7) && !app_loxone_intercom_snapshot(&snapshot));
    app_intercom_control_disconnected();
    assert(!s_remote.config.video_password[0] && !s_key[0]);
    assert(app_intercom_control_receive(status, 20));
    assert(!app_loxone_intercom_snapshot(&snapshot));
    assert(app_intercom_control_receive_profile(profile, extras));
    assert(app_loxone_intercom_snapshot(&snapshot));
    put32(status + 4, 12);
    assert(
        app_intercom_control_receive(status, 20) && !app_intercom_control_receive_profile(profile, extras));
    assert(!app_intercom_control_allowed(7));
    puts("PASS: real Slave receiver requires matching profile and explicit grant, heartbeat expiry, no "
         "public password, gate edge, brightness, cleanup and restart rejection");
    return 0;
}
#endif
