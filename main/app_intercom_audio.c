/* Közvetlen Gen2 SIP/TCP + G.711 RTP/UDP, kizárólag kézi hívásindítással.
 * A hálózat és a kodek egyetlen háttérfeladat tulajdona. LVGL-ből nincs I/O.
 * A mikrofon csak a sikeres SIP-válasz után nyílik meg, és minden kilépési ágon bezár. */
#include "app_intercom_audio.h"
#ifdef APP_INTERCOM_DISTRIBUTED
#include "app_intercom_control.h"
#include "app_ringtone_wav.h"
#endif
#include "app_intercom_audio_protocol.h"
#include "app_intercom_task.h"
#include "app_loxone.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_aec.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "app_intercom_build_config.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define SIP_PORT 5060
#ifdef APP_INTERCOM_SLAVE
#define AUDIO_RATE 24000
#define AUDIO_CAPTURE_CHANNELS 1
#else
#define AUDIO_RATE 16000
#define AUDIO_CAPTURE_CHANNELS 2
#endif
#define AUDIO_DECIMATION (AUDIO_RATE / 8000)
#define AUDIO_DSP_RATE 8000
#define AUDIO_FRAME_MAX 1024
static const char *TAG = "intercom_audio";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_requested, s_running, s_started;
static app_intercom_task_t s_audio_task = APP_INTERCOM_TASK_INITIALIZER;
static unsigned s_epoch, s_revision;
static app_intercom_audio_status_t s_status;
#ifdef APP_INTERCOM_DISTRIBUTED
static bool s_ringtone, s_call_pending;
static unsigned s_pending_revision;
#endif
static int64_t s_gate_stop_at;
static esp_codec_dev_handle_t s_speaker, s_microphone;
static void audio_worker(void *argument);

/* Újraindulás után is megmaradó, titkot nem tartalmazó hangművelet-jelző.
 * Nincs NVS/SD-írás; a videóhibák nem fedhetik el a hang crash helyét. */
typedef enum {
    AUDIO_STAGE_NONE, AUDIO_STAGE_START, AUDIO_STAGE_SIP, AUDIO_STAGE_I2S, AUDIO_STAGE_AEC,
    AUDIO_STAGE_SPEAKER, AUDIO_STAGE_MICROPHONE, AUDIO_STAGE_CAPTURE,
    AUDIO_STAGE_PROCESS, AUDIO_STAGE_PLAYBACK, AUDIO_STAGE_CLOSE, AUDIO_STAGE_REAP,
    AUDIO_STAGE_COUNT
} audio_stage_t;

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_system.h"
#include "app_events.h"
static RTC_NOINIT_ATTR volatile struct {
    uint32_t magic;
    uint32_t stage;
    uint32_t inverse;
} s_checkpoint;

static void audio_checkpoint(audio_stage_t stage)
{
    s_checkpoint.magic = 0;
    s_checkpoint.stage = stage;
    s_checkpoint.inverse = ~(uint32_t)stage;
    s_checkpoint.magic = 0x49434155U;
}

static void report_audio_reset(void)
{
    static const char *const names[AUDIO_STAGE_COUNT] = {
        "nincs", "hangfeladat indítása", "SIP hívás", "I2S indítás", "AEC visszhangszűrő",
        "ES8311 hangszóró", "ES7210 mikrofon", "mikrofon olvasása",
        "AEC feldolgozás", "hang lejátszása", "hang bontása", "feladat felszabadítása"
    };
    esp_reset_reason_t reason = esp_reset_reason();
    uint32_t stage = s_checkpoint.stage;
    if (s_checkpoint.magic == 0x49434155U && stage > AUDIO_STAGE_NONE && stage < AUDIO_STAGE_COUNT &&
        s_checkpoint.inverse == ~stage && reason != ESP_RST_POWERON && reason != ESP_RST_SW) {
        app_events_add(APP_EVENT_ERROR, "Hangkapcsolat közbeni újraindulás: %s (reset=%d).", names[stage], reason);
        ESP_LOGW(TAG, "Previous audio reset: stage=%s reason=%d", names[stage], reason);
    }
    audio_checkpoint(AUDIO_STAGE_NONE);
}
#else
static void audio_checkpoint(audio_stage_t stage) { (void)stage; }
static void report_audio_reset(void) {}
#endif

static void reap_audio(void)
{
    if (app_intercom_task_reap(&s_audio_task)) {
        audio_checkpoint(AUDIO_STAGE_NONE);
        portENTER_CRITICAL(&s_lock);
        s_running = false;
        portEXIT_CRITICAL(&s_lock);
    }
}

static void publish(unsigned epoch, app_intercom_audio_state_t state, const char *message)
{
    portENTER_CRITICAL(&s_lock);
    if (epoch == s_epoch) {
        s_status.state = state;
        s_status.revision = s_revision;
        snprintf(s_status.message, sizeof(s_status.message), "%s", message ? message : "");
    }
    portEXIT_CRITICAL(&s_lock);
}

bool app_intercom_audio_busy(void)
{
    reap_audio();
    portENTER_CRITICAL(&s_lock);
    bool busy = s_running;
    portEXIT_CRITICAL(&s_lock);
    return busy;
}

void app_intercom_audio_snapshot(app_intercom_audio_status_t *out)
{
    reap_audio();
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

bool app_intercom_audio_active_for(unsigned revision)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool active = s_requested && s_status.state == APP_INTERCOM_AUDIO_ACTIVE &&
                  s_revision == revision && (!s_gate_stop_at || now < s_gate_stop_at);
    portEXIT_CRITICAL(&s_lock);
    return active;
}

void app_intercom_audio_gate_opened(unsigned revision)
{
    int64_t deadline = esp_timer_get_time() + 5000000LL;
    portENTER_CRITICAL(&s_lock);
    if (s_requested && s_status.state == APP_INTERCOM_AUDIO_ACTIVE &&
        s_revision == revision && !s_gate_stop_at)
        s_gate_stop_at = deadline;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t audio_request(bool enabled, unsigned revision)
{
    reap_audio();
    app_intercom_snapshot_t current;
    if (enabled && (!app_loxone_intercom_snapshot(&current) ||
                    !current.ringing || !current.connected || current.revision != revision)) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    if (!enabled && s_revision != revision) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    if (enabled && (!s_started || s_running || s_requested)) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (enabled || s_requested || s_status.state == APP_INTERCOM_AUDIO_ERROR) {
        s_epoch++;
        s_revision = revision;
        s_requested = enabled;
        s_gate_stop_at = 0;
        if (enabled) {
            s_running = true;
        }
        s_status = (app_intercom_audio_status_t){
            .state = enabled ? APP_INTERCOM_AUDIO_CONNECTING :
                     s_running ? APP_INTERCOM_AUDIO_STOPPING : APP_INTERCOM_AUDIO_IDLE,
            .revision = revision,
        };
#ifdef APP_INTERCOM_DISTRIBUTED
        /* A fájl betöltése még nem SIP-kapcsolódás: a gomb már ekkor is
         * beszélgetést indítson, és előzze meg a csengőhang lejátszását. */
        if (enabled && s_ringtone) s_status.state = APP_INTERCOM_AUDIO_RINGING;
#endif
    }
    unsigned epoch = s_epoch;
#ifdef APP_INTERCOM_DISTRIBUTED
    bool ringtone_task = s_ringtone;
#endif
    portEXIT_CRITICAL(&s_lock);
    if (enabled)
        audio_checkpoint(AUDIO_STAGE_START);
    TaskHandle_t handle = NULL;
    unsigned stack_size = 16384;
    unsigned stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#ifdef APP_INTERCOM_DISTRIBUTED
    /* A WAV fájlműveletei belső vermet kapnak, mint a működő webes lejátszás.
     * A SIP/AEC nagy beszélgetési verme továbbra is PSRAM-ban marad. */
    if (ringtone_task) {
        stack_size = 8192;
        stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    }
#endif
    /* Az I2S által ütemezett hangot a GUI számolása nem szoríthatja ki. */
    if (enabled && xTaskCreatePinnedToCoreWithCaps(audio_worker, "intercom_audio", stack_size, NULL, 7, &handle, 0,
            stack_caps) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_running = false;
        s_requested = false;
        if (s_epoch == epoch) {
            s_status.state = APP_INTERCOM_AUDIO_ERROR;
            snprintf(s_status.message, sizeof(s_status.message), "%s", "Nincs elég memória a hang indításához.");
        } else if (s_status.state == APP_INTERCOM_AUDIO_STOPPING) {
            s_status.state = APP_INTERCOM_AUDIO_IDLE;
        }
        portEXIT_CRITICAL(&s_lock);
        audio_checkpoint(AUDIO_STAGE_NONE);
        return ESP_ERR_NO_MEM;
    }
    if (enabled)
        app_intercom_task_created(&s_audio_task, handle);
    return ESP_OK;
}

esp_err_t app_intercom_audio_request(bool enabled, unsigned revision)
{
#ifdef APP_INTERCOM_DISTRIBUTED
    reap_audio();
    portENTER_CRITICAL(&s_lock);
    bool preempt = enabled && s_running && s_ringtone;
    bool occupied = enabled && s_running && !preempt;
    if (!enabled && s_pending_revision == revision) s_call_pending = false;
    unsigned playing_revision = s_revision;
    if (preempt) { s_call_pending = true; s_pending_revision = revision; }
    portEXIT_CRITICAL(&s_lock);
    if (occupied) return ESP_ERR_INVALID_STATE;
    if (preempt) {
        (void)audio_request(false, playing_revision);
        return ESP_OK;
    }
    if (enabled && !app_intercom_control_acquire(revision)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = audio_request(enabled, revision);
    if (enabled && result != ESP_OK) app_intercom_control_release(revision);
    return result;
#else
    return audio_request(enabled, revision);
#endif
}

static bool wanted(unsigned epoch, unsigned revision)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool request = s_requested && s_epoch == epoch && (!s_gate_stop_at || now < s_gate_stop_at);
    bool active = s_status.state == APP_INTERCOM_AUDIO_ACTIVE;
    portEXIT_CRITICAL(&s_lock);
#ifdef APP_INTERCOM_DISTRIBUTED
    request = request && app_intercom_control_allowed(revision);
#endif
    return request && app_loxone_intercom_call_permitted(revision, active);
}

typedef struct {
    int tcp, udp, listener, incoming, rtp;
    struct sockaddr_in peer, media;
    unsigned epoch, revision;
    char local_ip[INET_ADDRSTRLEN], peer_ip[INET_ADDRSTRLEN];
    char uri[256], target[256], call_id[96], from[160], to[384], branch[64];
    uint16_t rtp_port, sip_local_port;
    uint8_t payload, formats;
    uint16_t receive_port;
    unsigned rx_packets, rx_accepted, rx_peer_rejected, rx_format_rejected, rx_invalid, tx_packets;
    unsigned volume;
    unsigned microphone_gain;
    bool accepted, established, pending, ended;
    intercom_sip_buffer_t stream, accepted_stream;
    char reply[INTERCOM_SIP_MAX + 1];
    intercom_jitter_t jitter;
    aec_handle_t *aec;
    int16_t *buffers;
    unsigned frame_samples;
    char hardware_error[128];
    bool speaker_open, microphone_open;
} audio_call_t;

static void nonblocking(int fd)
{
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

/* A TCP-küldés is rövid határidejű; leállításkor legfeljebb 200 ms-ot várunk. */
static bool send_tcp(int fd, const char *message)
{
    size_t used = 0, length = strlen(message);
    int64_t deadline = esp_timer_get_time() + 200000;
    while (used < length && esp_timer_get_time() < deadline) {
        int count = send(fd, message + used, length - used, 0);
        if (count > 0) {
            used += (size_t)count;
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            return false;
        }
    }
    return used == length;
}

static int bound_socket(int type, uint16_t port, uint16_t *actual)
{
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) {
        return -1;
    }
    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port),
                                  .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    socklen_t size = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &size) != 0) {
        close(fd);
        return -1;
    }
    if (actual) {
        *actual = ntohs(address.sin_port);
    }
    nonblocking(fd);
    return fd;
}

static bool connect_peer(audio_call_t *call, const char *host)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *resolved = NULL;
    if (getaddrinfo(host, "5060", &hints, &resolved) != 0 || !resolved) {
        return false;
    }
    memcpy(&call->peer, resolved->ai_addr, sizeof(call->peer));
    freeaddrinfo(resolved);
    if (!wanted(call->epoch, call->revision)) {
        return false;
    }
    inet_ntop(AF_INET, &call->peer.sin_addr, call->peer_ip, sizeof(call->peer_ip));
    call->tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (call->tcp < 0) {
        return false;
    }
    nonblocking(call->tcp);
    int result = connect(call->tcp, (struct sockaddr *)&call->peer, sizeof(call->peer));
    if (result < 0 && errno != EINPROGRESS) {
        return false;
    }
    int64_t deadline = esp_timer_get_time() + 5000000;
    while (result < 0) {
        if (!wanted(call->epoch, call->revision) || esp_timer_get_time() >= deadline) {
            return false;
        }
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(call->tcp, &write_set);
        struct timeval timeout = {.tv_usec = 100000};
        if (select(call->tcp + 1, NULL, &write_set, NULL, &timeout) > 0) {
            int error = 0;
            socklen_t length = sizeof(error);
            if (getsockopt(call->tcp, SOL_SOCKET, SO_ERROR, &error, &length) || error) {
                return false;
            }
            result = 0;
        }
    }
    struct sockaddr_in local;
    socklen_t length = sizeof(local);
    if (getsockname(call->tcp, (struct sockaddr *)&local, &length) != 0 ||
        !inet_ntop(AF_INET, &local.sin_addr, call->local_ip, sizeof(call->local_ip))) {
        return false;
    }
    call->sip_local_port = ntohs(local.sin_port);
    snprintf(call->uri, sizeof(call->uri), "sip:smarthome@%s:5060", call->peer_ip);
    snprintf(call->target, sizeof(call->target), "%s", call->uri);
    snprintf(call->call_id, sizeof(call->call_id), "%08lx%08lx@%s",
             (unsigned long)esp_random(), (unsigned long)esp_random(), call->local_ip);
    snprintf(call->from, sizeof(call->from), "<sip:display@%s>;tag=%08lx",
             call->local_ip, (unsigned long)esp_random());
    snprintf(call->to, sizeof(call->to), "<%s>", call->uri);
    snprintf(call->branch, sizeof(call->branch), "z9hG4bK%08lx", (unsigned long)esp_random());
    call->udp = bound_socket(SOCK_DGRAM, SIP_PORT, NULL);
    call->listener = bound_socket(SOCK_STREAM, SIP_PORT, NULL);
    call->rtp = bound_socket(SOCK_DGRAM, 0, &call->rtp_port);
    return call->udp >= 0 && call->listener >= 0 && call->rtp >= 0 && listen(call->listener, 1) == 0;
}

static bool sip_request(audio_call_t *call, const char *method, unsigned sequence, bool same_branch)
{
    char body[384] = "", branch[64];
    if (!strcmp(method, "INVITE")) {
        snprintf(body, sizeof(body),
                 "v=0\r\no=display 1 1 IN IP4 %s\r\ns=Intercom\r\nc=IN IP4 %s\r\nt=0 0\r\n"
                 "m=audio %u RTP/AVP 0 8\r\na=rtpmap:0 PCMU/8000\r\na=rtpmap:8 PCMA/8000\r\n"
                 "a=sendrecv\r\na=ptime:20\r\n", call->local_ip, call->local_ip, call->rtp_port);
    }
    snprintf(branch, sizeof(branch), "z9hG4bK%08lx", (unsigned long)esp_random());
    int count = snprintf(call->reply, sizeof(call->reply),
        "%s %s SIP/2.0\r\nVia: SIP/2.0/TCP %s:%u;branch=%s;rport\r\n"
        "From: %s\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: %u %s\r\nMax-Forwards: 70\r\n"
        "Contact: <sip:display@%s:5060>\r\nUser-Agent: ZoleeEnergyDisplay\r\n"
        "%sContent-Length: %u\r\n\r\n%s",
        method, same_branch ? call->uri : call->target, call->local_ip, call->sip_local_port,
        same_branch ? call->branch : branch, call->from, call->to, call->call_id,
        sequence, method, call->local_ip, body[0] ? "Content-Type: application/sdp\r\n" : "",
        (unsigned)strlen(body), body);
    return count > 0 && (size_t)count < sizeof(call->reply) && send_tcp(call->tcp, call->reply);
}

static bool same_dialog(audio_call_t *call, const char *message)
{
    char id[96];
    return intercom_sip_header(message, "Call-ID", id, sizeof(id)) && !strcmp(id, call->call_id);
}

static bool reply_request(audio_call_t *call, const char *message, int tcp,
                          const struct sockaddr_in *source)
{
    char via[512], from[384], to[384], id[96], cseq[96];
    if (!intercom_sip_header(message, "Via", via, sizeof(via)) ||
        !intercom_sip_header(message, "From", from, sizeof(from)) ||
        !intercom_sip_header(message, "To", to, sizeof(to)) ||
        !intercom_sip_header(message, "Call-ID", id, sizeof(id)) ||
        !intercom_sip_header(message, "CSeq", cseq, sizeof(cseq))) {
        return false;
    }
    bool bye = !strncmp(message, "BYE ", 4) && !strcmp(id, call->call_id) &&
               !strcmp(from, call->to) && !strcmp(to, call->from);
    const char *status = bye || !strncmp(message, "OPTIONS ", 8) ? "200 OK" : "481 Call Does Not Exist";
    if (!strncmp(message, "INVITE ", 7)) {
        status = "486 Busy Here";
    }
    int count = snprintf(call->reply, sizeof(call->reply),
        "SIP/2.0 %s\r\nVia: %s\r\nFrom: %s\r\nTo: %s\r\nCall-ID: %s\r\n"
        "CSeq: %s\r\nContent-Length: 0\r\n\r\n", status, via, from, to, id, cseq);
    if (count <= 0 || (size_t)count >= sizeof(call->reply)) {
        return false;
    }
    if (tcp >= 0) {
        (void)send_tcp(tcp, call->reply);
    } else {
        (void)sendto(call->udp, call->reply, (size_t)count, 0,
                     (const struct sockaddr *)source, sizeof(*source));
    }
    if (bye) {
        call->ended = true;
    }
    return true;
}

static bool contact_target(audio_call_t *call, const char *message)
{
    char contact[384];
    if (!intercom_sip_header(message, "Contact", contact, sizeof(contact))) {
        return false;
    }
    const char *start = strchr(contact, '<'), *end = strchr(contact, '>');
    if (!start || !end || end <= ++start || strncmp(start, "sip:", 4) || (size_t)(end - start) >= sizeof(call->target)) {
        return false;
    }
    char *at = memchr(start, '@', (size_t)(end - start));
    if (!at) {
        return false;
    }
    size_t ip_size = strlen(call->peer_ip);
    if ((size_t)(end - at - 1) < ip_size || strncmp(at + 1, call->peer_ip, ip_size)) {
        return false;
    }
    const char *suffix = at + 1 + ip_size;
    /* Csak a közvetlen készülék azonos portú célját fogadjuk el. */
    if (suffix != end && strncmp(suffix, ":5060", 5) && *suffix != ';') {
        return false;
    }
    if (suffix < end && *suffix == ':') {
        suffix += 5;
    }
    if (suffix != end && *suffix != ';') {
        return false;
    }
    memcpy(call->target, start, (size_t)(end - start));
    call->target[end - start] = 0;
    return true;
}

static bool handle_message(audio_call_t *call, char *message, int tcp, const struct sockaddr_in *source)
{
    if (strncmp(message, "SIP/2.0 ", 8)) {
        return reply_request(call, message, tcp, source);
    }
    if (!same_dialog(call, message)) {
        return true;
    }
    char cseq[64];
    if (!intercom_sip_header(message, "CSeq", cseq, sizeof(cseq)) || strcmp(cseq, "1 INVITE")) {
        return true;
    }
    int status = atoi(message + 8);
    if (status < 200) {
        return true;
    }
    if (call->established && status == 200) {
        return sip_request(call, "ACK", 1, false);
    }
    if (!intercom_sip_header(message, "To", call->to, sizeof(call->to))) {
        return false;
    }
    if (status != 200) {
        (void)sip_request(call, "ACK", 1, true);
        call->pending = false;
        ESP_LOGW(TAG, "SIP INVITE refused: %d", status);
        return false;
    }
    call->accepted = true;
    call->pending = false;
    char content_type[64], route[256];
    const char *sdp = strstr(message, "\r\n\r\n");
    uint16_t port;
    bool supported = sdp && intercom_sip_header(message, "Content-Type", content_type, sizeof(content_type)) &&
                     !strncasecmp(content_type, "application/sdp", 15) &&
                     !intercom_sip_header(message, "Record-Route", route, sizeof(route)) &&
                     contact_target(call, message) &&
                     intercom_sdp_audio_formats(sdp + 4, call->peer_ip, &port, &call->payload, &call->formats);
    /* A 200-as választ hibás SDP esetén is ACK + BYE zárja, nem maradhat nyitott hívás. */
    if (!sip_request(call, "ACK", 1, false) || !supported) {
        return false;
    }
    call->media = call->peer;
    call->media.sin_port = htons(port);
    call->established = true;
    ESP_LOGI(TAG, "RTP negotiated: local=%u remote=%u tx_payload=%u formats=%u",
             call->rtp_port, port, call->payload, call->formats);
    return true;
}

static bool pump_stream(audio_call_t *call, int fd, intercom_sip_buffer_t *buffer)
{
    if (buffer->used == INTERCOM_SIP_MAX) {
        return false;
    }
    int count = recv(fd, buffer->data + buffer->used, INTERCOM_SIP_MAX - buffer->used, 0);
    if (count == 0) {
        return false;
    }
    if (count < 0) {
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    }
    buffer->used += (size_t)count;
    buffer->data[buffer->used] = 0;
    while (buffer->used) {
        int size = intercom_sip_size(buffer->data, buffer->used);
        if (size < 0) {
            return false;
        }
        if (!size) {
            break;
        }
        char saved = buffer->data[size];
        buffer->data[size] = 0;
        bool valid = handle_message(call, buffer->data, fd, &call->peer);
        buffer->data[size] = saved;
        if (!valid) {
            return false;
        }
        memmove(buffer->data, buffer->data + size, buffer->used - (size_t)size);
        buffer->used -= (size_t)size;
        buffer->data[buffer->used] = 0;
    }
    return true;
}

static bool pump_signaling(audio_call_t *call)
{
    if (!pump_stream(call, call->tcp, &call->stream)) {
        return false;
    }
    if (call->incoming < 0) {
        struct sockaddr_in source;
        socklen_t length = sizeof(source);
        int incoming = accept(call->listener, (struct sockaddr *)&source, &length);
        if (incoming >= 0) {
            if (source.sin_addr.s_addr == call->peer.sin_addr.s_addr) {
                call->incoming = incoming;
                nonblocking(incoming);
                call->accepted_stream.used = 0;
            } else {
                close(incoming);
            }
        }
    }
    if (call->incoming >= 0 && !pump_stream(call, call->incoming, &call->accepted_stream)) {
        close(call->incoming);
        call->incoming = -1;
    }
    char packet[2049];
    struct sockaddr_in source;
    socklen_t length = sizeof(source);
    int count = recvfrom(call->udp, packet, sizeof(packet) - 1, 0, (struct sockaddr *)&source, &length);
    if (count > 0 && source.sin_addr.s_addr == call->peer.sin_addr.s_addr &&
        intercom_sip_size(packet, (size_t)count) == count) {
        packet[count] = 0;
        (void)handle_message(call, packet, -1, &source);
    }
    return !call->ended;
}

/* Minden lépés saját hibát ad; a DSP-hiba nem hangszóróhibaként jelenik meg. */
static bool hardware_check(audio_call_t *call, const char *stage, int result)
{
    if (result == ESP_OK) {
        return true;
    }
    snprintf(call->hardware_error, sizeof(call->hardware_error), "%s (%d)", stage, result);
    ESP_LOGW(TAG, "%s", call->hardware_error);
    return false;
}

/* A BSP hibás részleges inicializálás után nem nullázza a statikus csatornákat.
 * Egyszer próbáljuk: ismételt hívás nem használhat felszabadított BSP-handlet. */
static bool s_i2s_attempted;
static esp_err_t s_i2s_result;
static char s_i2s_error[128];

static esp_err_t prepare_i2s(void)
{
    if (s_i2s_attempted)
        return s_i2s_result;
    s_i2s_attempted = true;
    /* A BSP alapértéke 22050 Hz mono. Közös órajelen, kezdettől 16 kHz
     * Philips sztereó kell az ES8311 és a kétcsatornás ES7210 számára. */
#ifndef APP_INTERCOM_SLAVE
    const i2s_std_config_t i2s = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK, .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN,
        },
    };
    audio_checkpoint(AUDIO_STAGE_I2S);
    s_i2s_result = bsp_audio_init(&i2s);
#else
    /* BSP 3.0.1: a 4B mikrofonbemenete négy fizikai TDM-slotot használ.
     * FL=slot0, RE=slot1, FR=slot2; a kaputelefon egyetlen FL csatornát kér. */
    audio_checkpoint(AUDIO_STAGE_I2S);
    s_i2s_result = bsp_audio_init_voice_24k();
#endif
    snprintf(s_i2s_error, sizeof(s_i2s_error),
             "Hang I2S indítás (%d). DMA: %u B szabad, %u B legnagyobb blokk.%s",
             s_i2s_result,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             s_i2s_result == ESP_OK ? "" : " Újraindítás szükséges.");
    if (s_i2s_result != ESP_OK)
        ESP_LOGW(TAG, "%s", s_i2s_error);
    return s_i2s_result;
}

static bool open_hardware(audio_call_t *call)
{
    /* Az I2S belső DMA-területe előbb készül el, mint a DSP és a hálózati hangpufferek. */
    if (!hardware_check(call, "Hang I2C indítás", bsp_i2c_init()))
        return false;
    if (prepare_i2s() != ESP_OK) {
        snprintf(call->hardware_error, sizeof(call->hardware_error), "%s", s_i2s_error);
        return false;
    }
    /* A DSP és a mintapufferek PSRAM-ban élnek. A DMA saját belső puffereit
     * az I2S driver foglalja; a DSP nem fogyaszthatja el előlük a belső RAM-ot. */
    aec_config_t aec_config = {
        .mic_num = 1, .ref_num = 1, .out_num = 1,
        .filter_length = 4, .sample_rate = AUDIO_DSP_RATE,
        .caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        /* A készüléken az FD/FFT feldolgozás pánikot okozott. A SIP-híváshoz
         * a gyártó VoIP AEC-je 8 kHz-en közvetlenül illeszkedik a G.711-hez. */
        .mode = AEC_MODE_VOIP_LOW_COST,
        .nlp_level = AEC_NLP_LEVEL_AGGR,
    };
    audio_checkpoint(AUDIO_STAGE_AEC);
    call->aec = aec_create_from_config(&aec_config);
    if (!hardware_check(call, "Visszhangszűrő memória", call->aec && call->aec->aec_handle ? ESP_OK : ESP_ERR_NO_MEM)) {
        return false;
    }
    int frame = aec_get_chunksize(call->aec);
    /* A kerethosszt az algoritmus adja meg, nem a SIP csomagmérete.
     * Páros és 16 bájtra igazított csatornapufferek szükségesek a DSP-hez. */
    if (!hardware_check(call, "Visszhangszűrő keretméret",
                        frame > 0 && frame <= AUDIO_FRAME_MAX && frame % 8 == 0 ? ESP_OK : ESP_ERR_INVALID_SIZE)) {
        return false;
    }
    ESP_LOGI(TAG, "AEC VoIP: DSP rate=%u frame=%d; I2S rate=%u", AUDIO_DSP_RATE, frame, AUDIO_RATE);
    call->frame_samples = (unsigned)frame;
    call->buffers = heap_caps_aligned_calloc(16, frame * (AUDIO_DECIMATION * (2 + AUDIO_CAPTURE_CHANNELS) + 4), sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hardware_check(call, "Hangpuffer memória", call->buffers ? ESP_OK : ESP_ERR_NO_MEM) ||
        !wanted(call->epoch, call->revision)) {
        return false;
    }
    /* A gyártói duplex mintához hasonlóan mindkét kodek elkészül,
     * mielőtt megnyitnánk a közös adatillesztést. */
    if (!s_speaker) {
        audio_checkpoint(AUDIO_STAGE_SPEAKER);
        s_speaker = bsp_audio_codec_speaker_init();
    }
    if (!hardware_check(call, "ES8311 hangszóró inicializálás", s_speaker ? ESP_OK : ESP_FAIL)) {
        return false;
    }
    if (!s_microphone) {
        audio_checkpoint(AUDIO_STAGE_MICROPHONE);
        s_microphone = bsp_audio_codec_microphone_init();
    }
    if (!hardware_check(call, "ES7210 mikrofon inicializálás", s_microphone ? ESP_OK : ESP_FAIL)) {
        return false;
    }
    esp_codec_dev_sample_info_t format = {.sample_rate = AUDIO_RATE, .channel = 2, .bits_per_sample = 16};
    /* Sikertelen open után is bezárjuk az esetleg engedélyezett adatillesztést. */
    audio_checkpoint(AUDIO_STAGE_SPEAKER);
    call->speaker_open = true;
    if (!hardware_check(call, "Hangszóró megnyitás", esp_codec_dev_open(s_speaker, &format)) ||
        !hardware_check(call, "Hangszóró hangerő", esp_codec_dev_set_out_vol(s_speaker, (int)call->volume)) ||
        !wanted(call->epoch, call->revision)) {
        return false;
    }
    audio_checkpoint(AUDIO_STAGE_MICROPHONE);
    call->microphone_open = true;
    esp_codec_dev_sample_info_t microphone_format = format;
#ifdef APP_INTERCOM_SLAVE
    microphone_format.channel = BSP_AUDIO_TDM_SLOT_COUNT;
    microphone_format.channel_mask = BSP_AUDIO_TDM_SLOT_MASK_FL;
#endif
    return hardware_check(call, "Mikrofon megnyitás", esp_codec_dev_open(s_microphone, &microphone_format)) &&
        hardware_check(call, "Mikrofon feloldás", esp_codec_dev_set_in_mute(s_microphone, call->microphone_gain == 0)) &&
        hardware_check(call, "Hangszóró feloldás", esp_codec_dev_set_out_mute(s_speaker, call->volume == 0)) &&
        hardware_check(call, "Mikrofon erősítés", esp_codec_dev_set_in_gain(s_microphone, call->microphone_gain * 0.375f));
}

static bool receive_audio(audio_call_t *call, const struct sockaddr_in *source,
                          const uint8_t *packet, size_t size)
{
    call->rx_packets++;
    /* Az SDP portja a partner FOGADÓ portja, nem kötelező küldőport.
     * Az IP marad a SIP-ben ellenőrzött kaputelefoné; az első érvényes RTP
     * küldőportját és SSRC-jét a hívás végéig rögzítjük. A kimenet továbbra is SDP-célú. */
    if (source->sin_addr.s_addr != call->media.sin_addr.s_addr || !source->sin_port ||
        (call->receive_port && source->sin_port != call->receive_port)) {
        call->rx_peer_rejected++;
        return false;
    }
    uint8_t payload = size >= 2 ? packet[1] & 127 : 255;
    uint8_t format = payload == 0 ? 1 : payload == 8 ? 2 : 0;
    if (!(call->formats & format)) {
        call->rx_format_rejected++;
        return false;
    }
    if (!intercom_rtp_receive(&call->jitter, packet, size, payload)) {
        call->rx_invalid++;
        return false;
    }
    if (!call->receive_port) {
        call->receive_port = source->sin_port;
        ESP_LOGI(TAG, "RTP receive source=%u (SDP destination=%u), payload=%u",
                 ntohs(source->sin_port), ntohs(call->media.sin_port), payload);
    }
    call->rx_accepted++;
    return true;
}

static void media_session(audio_call_t *call)
{
    const unsigned samples = call->frame_samples;
    const unsigned frame = samples * AUDIO_DECIMATION;
    int16_t *speaker = call->buffers, *capture = speaker + frame * 2;
    int16_t *reference = capture + frame * AUDIO_CAPTURE_CHANNELS, *microphone = reference + samples;
    int16_t *clean = microphone + samples, *remote = clean + samples;
    int16_t transmit[160];
    size_t fill = 0;
    uint8_t packet[1200];
    uint16_t sequence = (uint16_t)esp_random();
    uint32_t timestamp = esp_random(), ssrc = esp_random();
    int64_t last_packet = esp_timer_get_time();
    publish(call->epoch, APP_INTERCOM_AUDIO_ACTIVE, "");
    /* A lejátszás és felvétel azonos I2S-órán fut. A hálózati csomagokból
     * legfeljebb 256 ms-nyi jitterpuffer él, hiányzó minták helyén csend van. */
    while (wanted(call->epoch, call->revision)) {
        if (!pump_signaling(call)) {
            if (!call->ended) {
                publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kaputelefon hangkapcsolata megszakadt.");
            }
            break;
        }
        for (unsigned i = 0; i < 16; i++) {
            struct sockaddr_in source;
            socklen_t length = sizeof(source);
            int count = recvfrom(call->rtp, packet, sizeof(packet), 0, (struct sockaddr *)&source, &length);
            if (count <= 0) {
                break;
            }
            if (receive_audio(call, &source, packet, (size_t)count)) {
                last_packet = esp_timer_get_time();
            }
        }
        if (esp_timer_get_time() - last_packet > 10000000LL) {
            ESP_LOGW(TAG, "RTP timeout: rx=%u accepted=%u peer=%u format=%u malformed=%u ssrc=%u late=%u ahead=%u resync=%u tx=%u",
                     call->rx_packets, call->rx_accepted, call->rx_peer_rejected,
                     call->rx_format_rejected, call->jitter.malformed, call->jitter.wrong_ssrc,
                     call->jitter.late_packets, call->jitter.ahead_packets, call->jitter.resyncs, call->tx_packets);
            publish(call->epoch, APP_INTERCOM_AUDIO_ERROR,
                    "A kaputelefon nem küld hangot. Indítsd újra a beszélgetést.");
            break;
        }
        intercom_jitter_read(&call->jitter, remote, samples);
        for (unsigned i = 0; i < frame; i++) {
            speaker[i * 2] = speaker[i * 2 + 1] = remote[i / AUDIO_DECIMATION];
        }
        /* Előbb kapjon adatot a DAC: a mikrofon és az AEC nem késleltetheti
         * a már fogadott hangblokk átadását. Az I2S olvasás/írás ugyanazon az órán fut. */
        audio_checkpoint(AUDIO_STAGE_PLAYBACK);
        if (esp_codec_dev_write(s_speaker, speaker, frame * 4) != ESP_CODEC_DEV_OK) {
            publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kijelző hangkimenete nem érhető el.");
            break;
        }
        if (!wanted(call->epoch, call->revision)) break;
        audio_checkpoint(AUDIO_STAGE_CAPTURE);
        if (esp_codec_dev_read(s_microphone, capture, frame * AUDIO_CAPTURE_CHANNELS * 2) != ESP_CODEC_DEV_OK) {
            publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kijelző mikrofonja nem ad hangot.");
            break;
        }
        if (!wanted(call->epoch, call->revision)) {
            break;
        }
        for (unsigned i = 0; i < samples; i++) {
            /* Master: 16 kHz sztereó első csatorna; Slave: a 24 kHz-es
             * TDM-ből tömörítve visszaadott FL csatorna. A DSP mindkettőn 8 kHz. */
            int sum = 0;
            for (unsigned n = 0; n < AUDIO_DECIMATION; n++)
                sum += capture[(i * AUDIO_DECIMATION + n) * AUDIO_CAPTURE_CHANNELS];
            microphone[i] = (int16_t)(sum / AUDIO_DECIMATION);
        }
        /* A referencia az előzőleg kiküldött hang; új mintát csak feldolgozás után írunk bele. */
        audio_checkpoint(AUDIO_STAGE_PROCESS);
        aec_process(call->aec, microphone, reference, clean);
        for (unsigned i = 0; i < samples; i++) {
            reference[i] = remote[i];
        }
        if (!wanted(call->epoch, call->revision)) {
            break;
        }
        /* A 8 kHz-es AEC kimenetéből változatlan időalappal 20 ms-os RTP készül. */
        for (unsigned i = 0; i < samples; i++) {
            transmit[fill++] = clean[i];
            if (fill == INTERCOM_RTP_SAMPLES) {
                size_t length = intercom_rtp_encode(packet, sequence++, timestamp, ssrc, call->payload, transmit);
                timestamp += INTERCOM_RTP_SAMPLES;
                if (sendto(call->rtp, packet, length, 0, (struct sockaddr *)&call->media, sizeof(call->media)) != (int)length) {
                    publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A hang küldése megszakadt. Próbáld újra.");
                    return;
                }
                call->tx_packets++;
                fill = 0;
            }
        }
        /* Az I2S órája adja az ütemet, nincs keretenkénti további várakozás. */
    }
    ESP_LOGI(TAG, "RTP totals: rx=%u accepted=%u peer=%u format=%u invalid=%u tx=%u",
             call->rx_packets, call->rx_accepted, call->rx_peer_rejected,
             call->rx_format_rejected, call->rx_invalid, call->tx_packets);
}

static void close_call(audio_call_t *call)
{
    audio_checkpoint(AUDIO_STAGE_CLOSE);
    /* Előbb a mikrofon és az erősítő áll le, csak utána történik hálózati bontás. */
    if (call->microphone_open) {
        esp_codec_dev_set_in_mute(s_microphone, true);
        esp_codec_dev_close(s_microphone);
    }
    if (call->speaker_open) {
        esp_codec_dev_set_out_mute(s_speaker, true);
        esp_codec_dev_close(s_speaker);
    }
    if (call->aec) {
        aec_destroy(call->aec);
    }
    free(call->buffers);
    if (call->pending && !call->accepted) {
        (void)sip_request(call, "CANCEL", 1, true);
        int64_t deadline = esp_timer_get_time() + 1000000;
        /* CANCEL/200 verseny esetén a késői hívást ACK + BYE bontja.
         * A mikrofon ekkor már biztosan zárva van. */
        while (call->pending && esp_timer_get_time() < deadline && pump_signaling(call)) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (call->accepted && !call->ended) {
        (void)sip_request(call, "BYE", 2, false);
    }
    int descriptors[] = {call->incoming, call->tcp, call->udp, call->listener, call->rtp};
    for (unsigned i = 0; i < sizeof(descriptors) / sizeof(descriptors[0]); i++) {
        if (descriptors[i] >= 0) {
            close(descriptors[i]);
        }
    }
}

static void run_call(audio_call_t *call, const app_intercom_snapshot_t *snapshot)
{
    call->volume = snapshot->config.speaker_volume;
    call->microphone_gain = snapshot->config.microphone_gain;
    audio_checkpoint(AUDIO_STAGE_SIP);
    publish(call->epoch, APP_INTERCOM_AUDIO_CONNECTING, "Kapcsolódás a kaputelefonhoz...");
    char host[128];
    if (!intercom_audio_host(snapshot->config.stream_url, host, sizeof(host)) || !connect_peer(call, host)) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kaputelefon SIP-hangkapcsolata nem érhető el (TCP 5060).");
        return;
    }
    if (!wanted(call->epoch, call->revision) || !sip_request(call, "INVITE", 1, true)) {
        return;
    }
    call->pending = true;
    publish(call->epoch, APP_INTERCOM_AUDIO_CONNECTING, "A kaputelefon válaszára várunk...");
    int64_t deadline = esp_timer_get_time() + 12000000;
    while (!call->established && wanted(call->epoch, call->revision) && esp_timer_get_time() < deadline) {
        if (!pump_signaling(call)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!call->established) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kaputelefon nem fogadta a kétirányú hanghívást.");
        return;
    }
    if (!wanted(call->epoch, call->revision)) {
        return;
    }
    publish(call->epoch, APP_INTERCOM_AUDIO_CONNECTING, "Hangkapcsolat előkészítése...");
    if (!open_hardware(call)) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, call->hardware_error);
        return;
    }
    media_session(call);
}

#ifdef APP_INTERCOM_DISTRIBUTED
static bool ringtone_wanted(unsigned epoch, unsigned revision)
{
    portENTER_CRITICAL(&s_lock);
    bool request = s_requested && s_epoch == epoch && s_ringtone;
    portEXIT_CRITICAL(&s_lock);
    app_intercom_snapshot_t current;
    return request && app_loxone_intercom_snapshot(&current) && current.connected &&
        current.ringing && current.revision == revision && !current.ringtone_silenced &&
        !current.audio_active;
}

static void ringtone_play(audio_call_t *call, const app_intercom_snapshot_t *snapshot)
{
    size_t size = 0, offset, bytes;
    uint8_t *data = NULL;
    app_ringtone_load_status_t load_status = APP_RINGTONE_LOAD_INVALID;
    char load_error[160] = "";
    /* Az SD rövid ideig foglalt lehet (pl. állapotmentés miatt). Ettől még
     * az egyszer lejátszandó csengőhang sem veszhet el. Hívás/gombnyomás
     * az ismétlések közti legfeljebb 10 ms várakozást is megszakíthatja. */
    for (unsigned attempt = 0; ringtone_wanted(call->epoch, call->revision); attempt++) {
        data = app_intercom_control_ringtone(snapshot->config.ringtone, &size, &load_status, load_error);
        if (data) break;
#ifdef APP_INTERCOM_SLAVE
        break;
#else
        /* PNG-/állománybetöltés a fél másodperces régi keretet túlléphette.
         * Csak átmeneti foglaltságot ismétlünk; hibás fájlt nem olvasunk 500-szor. */
        if (load_status != APP_RINGTONE_LOAD_BUSY || attempt >= 499) break;
        vTaskDelay(pdMS_TO_TICKS(10));
#endif
    }
    if (!ringtone_wanted(call->epoch, call->revision)) {
        free(data);
        return;
    }
    if (!data || !app_ringtone_wav(data, size, &offset, &bytes)) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR,
                load_error[0] ? load_error : app_ringtone_load_message(data ? APP_RINGTONE_LOAD_INVALID : load_status));
        free(data);
        return;
    }
    if (!hardware_check(call, "Hang I2C indítás", bsp_i2c_init()) || prepare_i2s() != ESP_OK) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, call->hardware_error[0] ? call->hardware_error : s_i2s_error);
        free(data);
        return;
    }
    if (!s_speaker) s_speaker = bsp_audio_codec_speaker_init();
    esp_codec_dev_sample_info_t format = {.sample_rate = AUDIO_RATE, .channel = 2, .bits_per_sample = 16};
    call->speaker_open = s_speaker != NULL;
    if (!hardware_check(call, "Hangszóró inicializálás", s_speaker ? ESP_OK : ESP_FAIL) ||
        !hardware_check(call, "Hangszóró megnyitás", esp_codec_dev_open(s_speaker, &format)) ||
        !hardware_check(call, "Hangszóró hangerő", esp_codec_dev_set_out_vol(s_speaker, snapshot->config.speaker_volume)) ||
        !hardware_check(call, "Hangszóró feloldás", esp_codec_dev_set_out_mute(s_speaker, snapshot->config.speaker_volume == 0))) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, call->hardware_error);
        free(data);
        return;
    }
    publish(call->epoch, APP_INTERCOM_AUDIO_RINGING, "");
    do {
        size_t frames = app_ringtone_frames((uint32_t)bytes, AUDIO_RATE);
        for (size_t position = 0; position < frames && ringtone_wanted(call->epoch, call->revision);) {
            int16_t output[320 * 2];
            size_t count = frames - position;
            if (count > 320) count = 320;
            for (size_t i = 0; i < count; i++) {
                size_t source = app_ringtone_source_frame((uint32_t)(position + i), AUDIO_RATE);
                const uint8_t *p = data + offset + source * 2;
                int16_t sample = (int16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
                output[i * 2] = output[i * 2 + 1] = sample;
            }
            if (esp_codec_dev_write(s_speaker, output, count * 4) != ESP_CODEC_DEV_OK) {
                publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A csengőhang lejátszása sikertelen.");
                free(data);
                return;
            }
            position += count;
        }
    } while (snapshot->config.ringtone_repeat && ringtone_wanted(call->epoch, call->revision));
    free(data);
}
#endif

static void audio_worker(void *argument)
{
    (void)argument;
    portENTER_CRITICAL(&s_lock);
    bool requested = s_requested;
#ifdef APP_INTERCOM_DISTRIBUTED
    bool ringtone = s_ringtone;
#endif
    unsigned epoch = s_epoch, revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    if (requested) {
        audio_call_t *call = calloc(1, sizeof(*call));
        app_intercom_snapshot_t snapshot;
        if (call) {
            call->tcp = call->udp = call->listener = call->incoming = call->rtp = -1;
            call->epoch = epoch;
            call->revision = revision;
#ifdef APP_INTERCOM_DISTRIBUTED
            if (ringtone && app_loxone_intercom_snapshot(&snapshot)) {
                ringtone_play(call, &snapshot);
            } else {
                {
                    int64_t deadline = esp_timer_get_time() + 3000000;
                    while (!app_intercom_control_allowed(revision) && esp_timer_get_time() < deadline) {
                        portENTER_CRITICAL(&s_lock);
                        bool waiting = s_requested && s_epoch == epoch;
                        portEXIT_CRITICAL(&s_lock);
                        if (!waiting || !app_loxone_intercom_call_permitted(revision, false)) break;
                        vTaskDelay(pdMS_TO_TICKS(20));
                    }
                }
#endif
            if (wanted(epoch, revision) && app_loxone_intercom_snapshot(&snapshot)) {
                run_call(call, &snapshot);
            }
#ifdef APP_INTERCOM_DISTRIBUTED
            }
#endif
            close_call(call);
            free(call);
        } else {
            publish(epoch, APP_INTERCOM_AUDIO_ERROR, "Nincs elég memória a hangkapcsolathoz.");
        }
    }
#ifdef APP_INTERCOM_DISTRIBUTED
    if (!ringtone) app_intercom_control_release(revision);
#endif
    portENTER_CRITICAL(&s_lock);
#ifdef APP_INTERCOM_DISTRIBUTED
    s_ringtone = false;
#endif
    if (s_epoch == epoch) {
        s_requested = false;
    }
    if (s_status.state != APP_INTERCOM_AUDIO_ERROR) {
        s_status.state = APP_INTERCOM_AUDIO_IDLE;
        s_status.message[0] = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    audio_checkpoint(AUDIO_STAGE_REAP);
    app_intercom_task_finish(&s_audio_task);
}

esp_err_t app_intercom_audio_start(void)
{
    report_audio_reset();
    /* Bootkor foglaljuk a kicsi, fix DMA-területet a kamera/AEC előtt,
     * akkor is, ha a kaputelefont később engedélyezik. A kodeket és a
     * mikrofont itt még nem nyitjuk meg. A videó audiohiba mellett is működik. */
    (void)prepare_i2s();
    audio_checkpoint(AUDIO_STAGE_NONE);
    portENTER_CRITICAL(&s_lock);
    s_started = true;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

void app_intercom_audio_poll(void)
{
#ifdef APP_INTERCOM_DISTRIBUTED
    reap_audio();
    app_intercom_snapshot_t current = {0};
    bool ready = app_loxone_intercom_snapshot(&current);
    portENTER_CRITICAL(&s_lock);
    bool pending = s_call_pending && !s_running;
    unsigned pending_revision = s_pending_revision;
    if (pending) s_call_pending = false;
    bool busy = s_running;
    portEXIT_CRITICAL(&s_lock);
    if (pending) {
        (void)app_intercom_audio_request(true, pending_revision);
        return;
    }
    static bool bell_seen;
    static unsigned bell_revision;
    bool ringing = ready && current.ringing && current.connected;
    if (!ringing) { bell_seen = false; return; }
    if (bell_seen && bell_revision == current.revision) return;
    if (busy) return;
    bell_seen = true;
    bell_revision = current.revision;
    if (!current.config.ringtone[0] || !current.config.speaker_volume || current.ringtone_silenced || current.audio_active) return;
    portENTER_CRITICAL(&s_lock);
    s_ringtone = true;
    portEXIT_CRITICAL(&s_lock);
    if (audio_request(true, current.revision) != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_ringtone = false;
        portEXIT_CRITICAL(&s_lock);
    }
#endif
}
