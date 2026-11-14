/* A valódi upstream küldő három függvénye ideiglenes include-ból fordul.
 * Csak az RTOS, a pufferfoglalás és a nyers transport határát helyettesítjük;
 * a TEXT/CONT/FIN darabolást és a visszatérési értéket az eredeti kód adja. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include "app_loxone_audio_auth.h"

typedef unsigned TickType_t;
typedef int ws_transport_opcodes_t;
typedef struct { bool held; } test_lock_t;
typedef struct
{
    void *transport;
    int buffer_size;
    char *tx_buffer;
    test_lock_t *lock;
    bool connected;
} test_client_t;
typedef test_client_t *esp_websocket_client_handle_t;
typedef struct { int last_error, esp_tls_error_code, esp_tls_flags; } *esp_tls_error_handle_t;

#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define WS_TRANSPORT_OPCODES_TEXT 1
#define WS_TRANSPORT_OPCODES_FIN 128
#define WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)

typedef struct { unsigned opcode, bytes; } test_frame_t;
static test_frame_t frames[4];
static unsigned frame_count, raw_calls, sent_bytes, allocations, releases;
static const char *expected;
static unsigned expected_length;
static bool allocation_failure;
static int raw_failure;

static bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client)
{
    return client->connected;
}

static int xSemaphoreTakeRecursive(test_lock_t *lock, TickType_t timeout)
{
    (void)timeout;
    assert(!lock->held);
    lock->held = true;
    return pdPASS;
}

static void xSemaphoreGiveRecursive(test_lock_t *lock)
{
    assert(lock->held);
    lock->held = false;
}

static int esp_websocket_new_buf(esp_websocket_client_handle_t client, bool transmit)
{
    assert(transmit && !client->tx_buffer);
    if (allocation_failure)
        return ESP_ERR_NO_MEM;
    client->tx_buffer = calloc(1, client->buffer_size);
    assert(client->tx_buffer);
    ++allocations;
    return ESP_OK;
}

static void esp_websocket_free_buf(esp_websocket_client_handle_t client, bool transmit)
{
    assert(transmit && client->tx_buffer);
    free(client->tx_buffer);
    client->tx_buffer = NULL;
    ++releases;
}

static int esp_transport_ws_send_raw(void *transport, ws_transport_opcodes_t opcode,
                                      const char *data, int length, int timeout)
{
    assert(transport && timeout == 1000 && length > 0);
    ++raw_calls;
    if (raw_failure)
        return raw_failure < 0 ? -1 : 0;
    assert(frame_count < sizeof(frames) / sizeof(frames[0]));
    assert((unsigned)length <= expected_length - sent_bytes);
    assert(!memcmp(data, expected + sent_bytes, (size_t)length));
    frames[frame_count++] = (test_frame_t){(unsigned)opcode, (unsigned)length};
    sent_bytes += (unsigned)length;
    return length;
}

static esp_tls_error_handle_t esp_transport_get_error_handle(void *transport)
{
    (void)transport;
    return NULL;
}

static const char *esp_err_to_name(int error)
{
    (void)error;
    return "fixture-error";
}

static void esp_websocket_client_error(esp_websocket_client_handle_t client,
                                        const char *format, ...)
{
    (void)client;
    (void)format;
}

static void esp_websocket_client_abort_connection(esp_websocket_client_handle_t client, int error)
{
    assert(error == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT && client->lock->held);
    client->connected = false;
}

/* Az upstream timeout kifejezése előjeles -1-et és unsigned tickértéket
 * vegyít. A host extra figyelmeztetésének kivétele csak a vendoré. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "audio_upstream_send.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void send_case(unsigned buffer_size, unsigned length, int failure)
{
    test_lock_t lock = {0};
    test_client_t client = {.transport = &lock, .buffer_size = buffer_size,
        .lock = &lock, .connected = true};
    char *payload = malloc((size_t)length + 1);
    assert(payload);
    for (unsigned i = 0; i < length; ++i)
        payload[i] = (char)('A' + i % 26);
    payload[length] = 0;
    expected = payload;
    expected_length = length;
    frame_count = raw_calls = sent_bytes = 0;
    allocation_failure = failure == 1;
    raw_failure = failure == 2 ? -1 : failure == 3 ? 1 : 0;
    unsigned before = allocations;
    int result = esp_websocket_client_send_text(&client, payload, (int)length, 1000);
    assert(!lock.held && !client.tx_buffer && allocations == releases);
    if (failure)
    {
        assert(result <= 0 && !frame_count && !sent_bytes);
        assert(raw_calls == (failure == 1 ? 0U : 1U));
        assert(allocations == before + (failure == 1 ? 0U : 1U));
    }
    else
    {
        assert(result == (int)length && sent_bytes == length);
        assert(frame_count == (length + buffer_size - 1) / buffer_size);
        for (unsigned i = 0; i < frame_count; ++i)
        {
            unsigned opcode = i ? 0 : WS_TRANSPORT_OPCODES_TEXT;
            if (i == frame_count - 1)
                opcode |= WS_TRANSPORT_OPCODES_FIN;
            assert(frames[i].opcode == opcode && frames[i].bytes <= buffer_size);
        }
        if (buffer_size == LX_AUDIO_AUTH_WS_BUFFER_SIZE)
            assert(frame_count == 1 && frames[0].opcode == (WS_TRANSPORT_OPCODES_TEXT | WS_TRANSPORT_OPCODES_FIN));
        else
            assert(frame_count > 1 && frames[0].opcode == WS_TRANSPORT_OPCODES_TEXT);
        printf("PASS: upstream TX buffer=%u, command=%u, frames=%u, complete payload preserved\n",
            buffer_size, length, frame_count);
    }
    free(payload);
    expected = NULL;
}

int main(void)
{
    _Static_assert(LX_AUDIO_AUTH_COMMAND_MAX + 1 <= LX_AUDIO_AUTH_WS_BUFFER_SIZE,
        "Az engedélyezett parancs és lezárója egyetlen pufferbe férjen");
    const unsigned lengths[] = {2112, LX_AUDIO_AUTH_COMMAND_MAX};
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i)
    {
        send_case(2048, lengths[i], 0);
        send_case(LX_AUDIO_AUTH_WS_BUFFER_SIZE, lengths[i], 0);
    }
    for (int failure = 1; failure <= 3; ++failure)
        send_case(LX_AUDIO_AUTH_WS_BUFFER_SIZE, LX_AUDIO_AUTH_COMMAND_MAX, failure);
    puts("PASS: upstream buffer OOM and zero/negative transport results stay errors with cleanup");
    return 0;
}
