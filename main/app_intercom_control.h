#pragma once
#include "app_loxone.h"
#include <stdint.h>

#define APP_INTERCOM_CONTROL_SIZE 20
#define APP_INTERCOM_PROFILE_SIZE 512
#define APP_INTERCOM_EXTRAS_SIZE 256
#define APP_INTERCOM_LEASE_US INT64_C(15000000)
#define APP_INTERCOM_FRESH_US INT64_C(3500000)

enum {
    APP_INTERCOM_ACQUIRE = 1,
    APP_INTERCOM_KEEP,
    APP_INTERCOM_RELEASE,
    APP_INTERCOM_GATE,
    APP_INTERCOM_CANCEL
};
enum {
    APP_INTERCOM_ENABLED = 1,
    APP_INTERCOM_RINGING = 2,
    APP_INTERCOM_CONNECTED = 4,
    APP_INTERCOM_GATE_READY = 8,
    APP_INTERCOM_PENDING = 16,
    APP_INTERCOM_HELD = 32,
    APP_INTERCOM_OWNED = 64,
    APP_INTERCOM_SILENCED = 128
};

/* A modell egyetlen zárolás alatt fut. A grant már SIP előtt létrejön;
 * release csak a socketek és a kodek teljes bontása után történhet. */
typedef struct {
    uint32_t owner, revision, grant;
    int64_t deadline, quarantine;
    bool silenced;
} app_intercom_arbiter_t;
void app_intercom_arbiter_tick(app_intercom_arbiter_t* state, uint32_t revision, int64_t now);
bool app_intercom_arbiter_command(app_intercom_arbiter_t* state, uint32_t owner, uint8_t operation,
    uint32_t grant, bool available, int64_t now);

/* Helyi hangfeladat és a hitelesített BLE adapter közötti kapu. */
void app_intercom_control_init(const app_config_t* config);
void app_intercom_control_brightness(unsigned* day, unsigned* night);
bool app_intercom_control_acquire(unsigned revision);
bool app_intercom_control_allowed(unsigned revision);
void app_intercom_control_release(unsigned revision);
bool app_intercom_control_held(unsigned revision);
bool app_intercom_control_other(unsigned revision);
bool app_intercom_control_silenced(unsigned revision);
void app_intercom_control_gate(unsigned revision);
void app_intercom_control_tick(void);
void app_intercom_control_status(uint32_t peer, uint8_t out[APP_INTERCOM_CONTROL_SIZE]);
bool app_intercom_control_command(uint32_t peer, const uint8_t* data, size_t size);
bool app_intercom_control_profile(unsigned part, uint8_t* data);
void app_intercom_control_disconnected(void);
bool app_intercom_control_receive(const uint8_t* data, size_t size);
bool app_intercom_control_receive_profile(const uint8_t* profile, const uint8_t* extras);
bool app_intercom_control_outgoing(uint8_t out[APP_INTERCOM_CONTROL_SIZE]);
void app_intercom_control_sent(bool success);
/* Csengőhang lekérése csak a BLE-n kapott, véletlen hozzáférési kulccsal. */
bool app_intercom_control_media_key(const char* key);
uint8_t* app_intercom_control_ringtone(const char* name, size_t* size);
