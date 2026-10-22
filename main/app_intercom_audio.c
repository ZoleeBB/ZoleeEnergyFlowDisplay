/* Közvetlen Gen2 SIP/TCP + G.711 RTP/UDP, kizárólag kézi hívásindítással.
 * A hálózat és a kodek egyetlen háttérfeladat tulajdona. LVGL-ből nincs I/O.
 * A mikrofon csak a sikeres SIP-válasz után nyílik meg, és minden kilépési ágon bezár. */
#include "app_intercom_audio.h"
#include "app_intercom_audio_protocol.h"
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
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SIP_PORT 5060
#define AUDIO_RATE 16000
#define AUDIO_FRAME 512
static const char *TAG = "intercom_audio";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_requested, s_running, s_started;
static unsigned s_epoch, s_revision;
static app_intercom_audio_status_t s_status;
static esp_codec_dev_handle_t s_speaker, s_microphone;

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

void app_intercom_audio_snapshot(app_intercom_audio_status_t *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

esp_err_t app_intercom_audio_request(bool enabled, unsigned revision)
{
    app_intercom_snapshot_t current;
    if (enabled && (!app_loxone_intercom_snapshot(&current) || !current.ringing ||
                    !current.connected || current.revision != revision)) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    if (enabled && (!s_started || s_running || s_requested)) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (enabled || s_requested || s_status.state == APP_INTERCOM_AUDIO_ERROR) {
        s_epoch++;
        s_revision = revision;
        s_requested = enabled;
        s_status = (app_intercom_audio_status_t){
            .state = enabled ? APP_INTERCOM_AUDIO_CONNECTING :
                     s_running ? APP_INTERCOM_AUDIO_STOPPING : APP_INTERCOM_AUDIO_IDLE,
            .revision = revision,
        };
    }
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static bool wanted(unsigned epoch, unsigned revision)
{
    portENTER_CRITICAL(&s_lock);
    bool request = s_requested && s_epoch == epoch;
    portEXIT_CRITICAL(&s_lock);
    app_intercom_snapshot_t current;
    return request && app_loxone_intercom_snapshot(&current) && current.ringing &&
           current.connected && current.revision == revision;
}

typedef struct {
    int tcp, udp, listener, incoming, rtp;
    struct sockaddr_in peer, media;
    unsigned epoch, revision;
    char local_ip[INET_ADDRSTRLEN], peer_ip[INET_ADDRSTRLEN];
    char uri[256], target[256], call_id[96], from[160], to[384], branch[64];
    uint16_t rtp_port, sip_local_port;
    uint8_t payload;
    bool accepted, established, pending, ended;
    intercom_sip_buffer_t stream, accepted_stream;
    char reply[INTERCOM_SIP_MAX + 1];
    intercom_jitter_t jitter;
    aec_handle_t *aec;
    int16_t *buffers;
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
                     intercom_sdp_audio(sdp + 4, call->peer_ip, &port, &call->payload);
    /* A 200-as választ hibás SDP esetén is ACK + BYE zárja, nem maradhat nyitott hívás. */
    if (!sip_request(call, "ACK", 1, false) || !supported) {
        return false;
    }
    call->media = call->peer;
    call->media.sin_port = htons(port);
    call->established = true;
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

static bool open_hardware(audio_call_t *call)
{
    /* A BSP ugyanazt az I2C-buszt használja, mint az érintőpanel. Nem telepítünk másikat.
     * A két ADC-csatornát Philips sztereó keretben olvassuk, az első a beszédmikrofon. */
    if (bsp_i2c_init() != ESP_OK || bsp_audio_init(NULL) != ESP_OK) {
        return false;
    }
    if (!s_speaker) {
        s_speaker = bsp_audio_codec_speaker_init();
    }
    if (!s_microphone) {
        s_microphone = bsp_audio_codec_microphone_init();
    }
    if (!s_speaker || !s_microphone) {
        return false;
    }
    esp_codec_dev_sample_info_t format = {.sample_rate = AUDIO_RATE, .channel = 2, .bits_per_sample = 16};
    call->aec = aec_create(AUDIO_RATE, 4, 1, AEC_MODE_FD_LOW_COST);
    if (!call->aec || aec_get_chunksize(call->aec) != AUDIO_FRAME) {
        return false;
    }
    call->buffers = heap_caps_aligned_calloc(16, AUDIO_FRAME * 8, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!call->buffers || esp_codec_dev_open(s_speaker, &format) != ESP_CODEC_DEV_OK) {
        return false;
    }
    call->speaker_open = true;
    if (esp_codec_dev_set_out_vol(s_speaker, 55) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_open(s_microphone, &format) != ESP_CODEC_DEV_OK) {
        return false;
    }
    call->microphone_open = true;
    if (esp_codec_dev_set_out_mute(s_speaker, false) != ESP_CODEC_DEV_OK) {
        return false;
    }
    return esp_codec_dev_set_in_gain(s_microphone, 24.0f) == ESP_CODEC_DEV_OK;
}

static void media_session(audio_call_t *call)
{
    int16_t *speaker = call->buffers, *capture = speaker + AUDIO_FRAME * 2;
    int16_t *reference = capture + AUDIO_FRAME * 2, *microphone = reference + AUDIO_FRAME;
    int16_t *clean = microphone + AUDIO_FRAME, *remote = clean + AUDIO_FRAME;
    int16_t transmit[160];
    size_t fill = 0;
    uint8_t packet[1200];
    uint16_t sequence = (uint16_t)esp_random();
    uint32_t timestamp = esp_random(), ssrc = esp_random();
    int64_t last_packet = esp_timer_get_time();
    publish(call->epoch, APP_INTERCOM_AUDIO_ACTIVE, "Beszélgetés folyamatban");
    /* A lejátszás és felvétel azonos I2S-órán fut. A hálózati csomagokból
     * legfeljebb 256 ms-nyi jitterpuffer él, hiányzó minták helyén csend van. */
    while (wanted(call->epoch, call->revision) && pump_signaling(call)) {
        for (unsigned i = 0; i < 16; i++) {
            struct sockaddr_in source;
            socklen_t length = sizeof(source);
            int count = recvfrom(call->rtp, packet, sizeof(packet), 0, (struct sockaddr *)&source, &length);
            if (count <= 0) {
                break;
            }
            if (source.sin_addr.s_addr == call->media.sin_addr.s_addr && source.sin_port == call->media.sin_port &&
                intercom_rtp_receive(&call->jitter, packet, (size_t)count, call->payload)) {
                last_packet = esp_timer_get_time();
            }
        }
        if (esp_timer_get_time() - last_packet > 10000000LL) {
            publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "Nem érkezik hang a kaputelefontól. Próbáld újra.");
            break;
        }
        intercom_jitter_read(&call->jitter, remote, AUDIO_FRAME / 2);
        for (unsigned i = 0; i < AUDIO_FRAME; i++) {
            speaker[i * 2] = speaker[i * 2 + 1] = remote[i / 2];
        }
        if (esp_codec_dev_read(s_microphone, capture, AUDIO_FRAME * 4) != ESP_CODEC_DEV_OK ||
            !wanted(call->epoch, call->revision)) {
            break;
        }
        for (unsigned i = 0; i < AUDIO_FRAME; i++) {
            microphone[i] = capture[i * 2];
        }
        /* A referencia az előzőleg kiküldött hang; új mintát csak feldolgozás után írunk bele. */
        aec_process(call->aec, microphone, reference, clean);
        if (esp_codec_dev_write(s_speaker, speaker, AUDIO_FRAME * 4) != ESP_CODEC_DEV_OK) {
            publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kijelző hangkimenete nem érhető el.");
            break;
        }
        for (unsigned i = 0; i < AUDIO_FRAME; i++) {
            reference[i] = speaker[i * 2];
        }
        /* 16 kHz AEC -> 8 kHz G.711; a 32 ms-os DSP-keretből 20 ms-os RTP-csomagok készülnek. */
        for (unsigned i = 0; i < AUDIO_FRAME; i += 2) {
            transmit[fill++] = (int16_t)(((int)clean[i] + clean[i + 1]) / 2);
            if (fill == INTERCOM_RTP_SAMPLES) {
                size_t length = intercom_rtp_encode(packet, sequence++, timestamp, ssrc, call->payload, transmit);
                timestamp += INTERCOM_RTP_SAMPLES;
                if (sendto(call->rtp, packet, length, 0, (struct sockaddr *)&call->media, sizeof(call->media)) != (int)length) {
                    publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A hang küldése megszakadt. Próbáld újra.");
                    return;
                }
                fill = 0;
            }
        }
        vTaskDelay(1);
    }
}

static void close_call(audio_call_t *call)
{
    /* Előbb a mikrofon és az erősítő áll le, csak utána történik hálózati bontás. */
    if (call->microphone_open) {
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
    char host[128];
    if (!intercom_audio_host(snapshot->config.stream_url, host, sizeof(host)) || !connect_peer(call, host)) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kaputelefon SIP-hangkapcsolata nem érhető el (TCP 5060).");
        return;
    }
    if (!wanted(call->epoch, call->revision) || !sip_request(call, "INVITE", 1, true)) {
        return;
    }
    call->pending = true;
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
    if (!open_hardware(call)) {
        publish(call->epoch, APP_INTERCOM_AUDIO_ERROR, "A kijelző mikrofonja vagy hangszórója nem indítható.");
        return;
    }
    media_session(call);
}

static void audio_worker(void *argument)
{
    (void)argument;
    for (;;) {
        portENTER_CRITICAL(&s_lock);
        bool requested = s_requested;
        unsigned epoch = s_epoch, revision = s_revision;
        s_running = requested;
        portEXIT_CRITICAL(&s_lock);
        if (requested) {
            audio_call_t *call = calloc(1, sizeof(*call));
            app_intercom_snapshot_t snapshot;
            if (call) {
                call->tcp = call->udp = call->listener = call->incoming = call->rtp = -1;
                call->epoch = epoch;
                call->revision = revision;
                if (wanted(epoch, revision) && app_loxone_intercom_snapshot(&snapshot)) {
                    run_call(call, &snapshot);
                }
                close_call(call);
                free(call);
            } else {
                publish(epoch, APP_INTERCOM_AUDIO_ERROR, "Nincs elég memória a hangkapcsolathoz.");
            }
            portENTER_CRITICAL(&s_lock);
            s_running = false;
            if (s_epoch == epoch) {
                s_requested = false;
            }
            if (s_status.state != APP_INTERCOM_AUDIO_ERROR) {
                s_status.state = APP_INTERCOM_AUDIO_IDLE;
                s_status.message[0] = 0;
            }
            portEXIT_CRITICAL(&s_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t app_intercom_audio_start(void)
{
    if (s_started) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCore(audio_worker, "intercom_audio", 16384, NULL, 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}
