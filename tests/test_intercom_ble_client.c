/* A tényleges opcionális ATT kliens és Slave állapotkapu együtt: darabolt
 * profil, közben változó Master-generáció, grant/KEEP és időtúllépés. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../main/app_intercom_control.c"
#define BLE_HS_EDONE 14
struct os_mbuf {
    size_t len;
    uint8_t data[512];
};
struct ble_gatt_error {
    int status;
};
struct ble_gatt_attr {
    uint16_t offset;
    struct os_mbuf* om;
};
#define OS_MBUF_PKTLEN(m) ((m)->len)
typedef int (*callback_t)(uint16_t, const struct ble_gatt_error*, struct ble_gatt_attr*, void*);
static callback_t callback;
static void* callback_arg;
static unsigned requested, request_kind, request_count;
static uint8_t written[20];
static int64_t test_now = 1000000;
static uint16_t s_master = 7;
static bool s_slave_ready = true, s_profile_reading, s_profile_waiting, s_presence_reading;
static bool authenticated = true;
int64_t esp_timer_get_time(void)
{
    return test_now;
}
bool app_intercom_audio_busy(void)
{
    return false;
}
void app_intercom_audio_gate_opened(unsigned revision)
{
    (void)revision;
}
static bool security_authenticated(uint16_t conn)
{
    assert(conn == 7);
    return authenticated;
}
static bool slave_stats_transport_busy(void)
{
    return false;
}
static int os_mbuf_copydata(struct os_mbuf* m, size_t offset, size_t size, void* out)
{
    if (offset + size > m->len)
        return -1;
    memcpy(out, m->data + offset, size);
    return 0;
}
static int request(uint16_t conn, uint16_t handle, callback_t cb, void* arg, unsigned kind)
{
    assert(conn == 7 && !callback);
    requested = handle;
    callback = cb;
    callback_arg = arg;
    request_kind = kind;
    request_count++;
    return 0;
}
static int ble_gattc_read(uint16_t c, uint16_t h, callback_t cb, void* a)
{
    return request(c, h, cb, a, 1);
}
static int ble_gattc_read_long(uint16_t c, uint16_t h, unsigned offset, callback_t cb, void* a)
{
    assert(!offset);
    return request(c, h, cb, a, 2);
}
static int ble_gattc_write_flat(
    uint16_t c, uint16_t h, const uint8_t* data, size_t size, callback_t cb, void* a)
{
    assert(size == 20);
    memcpy(written, data, 20);
    return request(c, h, cb, a, 3);
}
#include "../main/app_ble_intercom_client.inc"
static void complete(const uint8_t* data, size_t size)
{
    callback_t cb = callback;
    void* arg = callback_arg;
    assert(cb);
    callback = NULL;
    struct ble_gatt_error ok = { 0 }, end = { BLE_HS_EDONE };
    struct os_mbuf mbuf;
    struct ble_gatt_attr attr = { .om = &mbuf };
    if (request_kind == 3) {
        cb(7, &ok, NULL, arg);
        return;
    }
    if (request_kind == 1) {
        assert(size <= sizeof(mbuf.data));
        memcpy(mbuf.data, data, size);
        mbuf.len = size;
        cb(7, &ok, &attr, arg);
        return;
    }
    for (size_t p = 0; p < size;) {
        size_t n = size - p;
        if (n > 19)
            n = 19;
        mbuf.len = n;
        memcpy(mbuf.data, data + p, n);
        attr.offset = p;
        cb(7, &ok, &attr, arg);
        p += n;
    }
    cb(7, &end, NULL, arg);
}
static void poll(void)
{
    test_now += 500000;
    slave_intercom_poll(test_now);
}
int main(void)
{
    slave_intercom_reset();
    s_remote_intercom = 170;
    s_remote_intercom_profile = 180;
    s_remote_intercom_extras = 190;
    uint8_t status[20] = { 1 }, profile[512] = { 1 }, extras[256] = { 1 };
    put32(status + 4, 11);
    put32(status + 8, 7);
    status[1] = APP_INTERCOM_ENABLED | APP_INTERCOM_RINGING | APP_INTERCOM_CONNECTED;
    poll();
    assert(requested == 170 && request_kind == 1);
    complete(status, 20);
    poll();
    assert(requested == 180 && request_kind == 2);
    /* Master-generációváltás a hosszú profilolvasás alatt. */
    memcpy(profile + 4, status + 4, 8);
    put32(profile + 8, 8);
    memcpy(extras + 4, profile + 4, 8);
    strcpy((char*)profile + 12, "http://192.168.1.20/mjpg/video.mjpg");
    strcpy((char*)profile + 269, "admin");
    strcpy((char*)profile + 365, "secret");
    profile[493] = 55;
    profile[494] = 80;
    profile[496] = 1;
    extras[12] = 82;
    extras[13] = 17;
    complete(profile, 512);
    poll();
    assert(requested == 190);
    complete(extras, 256);
    assert(!s_profile_ready);
    poll();
    assert(requested == 170 && request_kind == 1);
    put32(status + 8, 8);
    complete(status, 20);
    poll();
    assert(requested == 180);
    complete(profile, 512);
    poll();
    assert(requested == 190);
    complete(extras, 256);
    assert(s_profile_ready);
    poll();
    assert(requested == 170);
    complete(status, 20);
    assert(app_intercom_control_acquire(8));
    poll();
    assert(requested == 170 && request_kind == 3 && written[1] == APP_INTERCOM_ACQUIRE);
    complete(NULL, 0);
    poll();
    assert(request_kind == 1);
    status[1] |= APP_INTERCOM_OWNED | APP_INTERCOM_HELD;
    status[3] = 1;
    put32(status + 12, 99);
    complete(status, 20);
    assert(app_intercom_control_allowed(8));
    poll();
    assert(request_kind == 3 && written[1] == APP_INTERCOM_KEEP);
    complete(NULL, 0);
    poll();
    assert(request_kind == 1);
    unsigned count = request_count;
    test_now += APP_INTERCOM_FRESH_US + 1;
    slave_intercom_poll(test_now);
    assert(!app_intercom_control_allowed(8) && request_count == count);
    complete(status, 20);
    assert(!s_intercom_reading && !s_profile_ready);
    poll();
    assert(requested == 170 && request_kind == 1);
    complete(status, 20);
    /* Hiányos/másik generációjú profil nem publikálható. */
    poll();
    assert(requested == 180);
    complete(profile, 511);
    assert(!s_profile_ready && !s_intercom_reading);
    poll();
    assert(requested == 170);
    complete(status, 20);
    slave_intercom_reset();
    assert(!s_remote_intercom && !s_profile_ready && !s_received);
    puts("PASS: real 20-byte ATT control, 19-byte profile fragments, mid-read generation recovery, mandatory "
         "status after grant/KEEP/profile, expired callback and incomplete profile rejection");
    return 0;
}
