/* A valódi audio worker helyi TCP/UDP kaputelefonnal; csak az I2S/AEC hardver hamis. */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

/* A helyi teszt két loopback-címmel ugyanazt az 5060-as SIP-portot használja. */
static int local_bind(int fd, const struct sockaddr *source, socklen_t size)
{
    struct sockaddr_in address = *(const struct sockaddr_in *)source;
    if (address.sin_addr.s_addr == htonl(INADDR_ANY)) {
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    }
    return bind(fd, (struct sockaddr *)&address, size);
}
#define bind local_bind
#include "../main/app_intercom_audio.c"
#undef bind

static atomic_bool ring = true;
static int reads, writes, mic_opens, mic_closes, speaker_closes;
static unsigned tasks_created, tasks_deleted;
static bool task_failure;
static bool fail_microphone, muted, speaker_audio;

void vTaskDelay(unsigned delay)
{
    struct timespec time = {.tv_sec=delay / 1000, .tv_nsec=(long)(delay % 1000) * 1000000L};
    nanosleep(&time, NULL);
}
int xTaskCreatePinnedToCore(void (*worker)(void *), const char *name, unsigned stack,
                           void *argument, unsigned priority, void *handle, unsigned core)
{
    (void)worker; (void)name; (void)stack; (void)argument; (void)priority; (void)handle; (void)core;
    if (task_failure) return 0;
    tasks_created++;
    return pdPASS;
}
void vTaskDelete(void *task)
{
    assert(!task);
    tasks_deleted++;
}
int64_t esp_timer_get_time(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (int64_t)time.tv_sec * 1000000LL + time.tv_nsec / 1000;
}
uint32_t esp_random(void)
{
    static uint32_t value = 128;
    return ++value;
}
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, unsigned caps)
{
    (void)caps;
    void *memory = NULL;
    if (posix_memalign(&memory, alignment, count * size) != 0) return NULL;
    memset(memory, 0, count * size);
    return memory;
}
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
esp_err_t bsp_audio_init(const void *configuration) { (void)configuration; return ESP_OK; }
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void) { return (void *)1; }
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void) { return (void *)2; }
int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *format)
{
    assert(format->sample_rate == 16000 && format->channel == 2 && format->bits_per_sample == 16);
    if (handle == (void *)2) {
        if (fail_microphone) return -1;
        mic_opens++;
    }
    return 0;
}
int esp_codec_dev_close(esp_codec_dev_handle_t handle)
{
    if (handle == (void *)2) mic_closes++; else speaker_closes++;
    return 0;
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume)
{
    (void)handle; assert(volume > 0 && volume <= 60); return 0;
}
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t handle, bool mute)
{
    (void)handle; muted = mute; return 0;
}
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t handle, bool mute)
{
    (void)mute; assert(handle == (void *)2); return 0;
}
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t handle, float gain)
{
    (void)handle; assert(gain <= 24.0f); return 0;
}
int esp_codec_dev_read(esp_codec_dev_handle_t handle, void *data, int size)
{
    assert(handle == (void *)2 && size == AUDIO_FRAME * 4);
    for (int i = 0; i < size / 2; i++) ((int16_t *)data)[i] = 4000;
    reads++;
    vTaskDelay(32);
    return 0;
}
int esp_codec_dev_write(esp_codec_dev_handle_t handle, void *data, int size)
{
    assert(handle == (void *)1 && size == AUDIO_FRAME * 4 && data && !muted);
    for (int i = 0; i < size / 2; i++) {
        if (abs(((int16_t *)data)[i]) > 100) speaker_audio = true;
    }
    writes++;
    /* Az explicit csengetésvég az aktív mikrofont is leállítja. */
    if (writes == 5) atomic_store(&ring, false);
    return 0;
}
aec_handle_t *aec_create(int rate, int filter, int channels, int mode)
{
    assert(rate == 16000 && filter == 4 && channels == 1 && mode == AEC_MODE_FD_LOW_COST);
    aec_handle_t *handle = calloc(1, sizeof(*handle));
    if (handle) handle->frame_size = AUDIO_FRAME;
    return handle;
}
int aec_get_chunksize(aec_handle_t *aec) { return aec->frame_size; }
void aec_process(aec_handle_t *aec, int16_t *input, int16_t *reference, int16_t *output)
{
    assert(aec && input && reference && output);
    memcpy(output, input, AUDIO_FRAME * 2);
}
void aec_destroy(aec_handle_t *aec) { free(aec); }
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    *out = (app_intercom_snapshot_t){.ringing=atomic_load(&ring),.connected=true,.revision=7};
    strcpy(out->config.stream_url, "http://127.0.0.2/mjpg/video.mjpg");
    return true;
}

typedef struct { int listener, media, sip, mode; bool ack, bye, cancelled, sent_bye; unsigned packets; } fake_door_t;
static void receive_sip(int fd, intercom_sip_buffer_t *buffer)
{
    while (intercom_sip_size(buffer->data, buffer->used) == 0) {
        int count = recv(fd, buffer->data + buffer->used, INTERCOM_SIP_MAX - buffer->used, 0);
        assert(count > 0);
        buffer->used += (size_t)count;
        buffer->data[buffer->used] = 0;
    }
    assert(intercom_sip_size(buffer->data, buffer->used) > 0);
}
static void *fake_door(void *argument)
{
    fake_door_t *door = argument;
    int fd = accept(door->listener, NULL, NULL);
    assert(fd >= 0);
    struct timeval timeout = {.tv_sec=4};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    intercom_sip_buffer_t *buffer = calloc(1, sizeof(*buffer));
    receive_sip(fd, buffer);
    assert(!strncmp(buffer->data, "INVITE sip:smarthome@127.0.0.2:5060 SIP/2.0", 40));
    char via[512], from[384], to[384], id[96];
    assert(intercom_sip_header(buffer->data, "Via", via, sizeof(via)));
    assert(intercom_sip_header(buffer->data, "From", from, sizeof(from)));
    assert(intercom_sip_header(buffer->data, "To", to, sizeof(to)));
    assert(intercom_sip_header(buffer->data, "Call-ID", id, sizeof(id)));
    const char *audio = strstr(buffer->data, "m=audio ");
    assert(audio);
    unsigned port;
    assert(sscanf(audio, "m=audio %u", &port) == 1);
    struct sockaddr_in destination = {.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
    inet_pton(AF_INET, "127.0.0.1", &destination.sin_addr);
    struct sockaddr_in local;
    socklen_t length = sizeof(local);
    assert(getsockname(door->media, (struct sockaddr *)&local, &length) == 0);
    char sdp[256], response[2048];
    int body = snprintf(sdp, sizeof(sdp), "v=0\r\nc=IN IP4 127.0.0.2\r\nm=audio %u RTP/AVP 0\r\na=sendrecv\r\n", ntohs(local.sin_port));
    if (door->mode == 1) {
        /* A leállítás és a későn érkező 200-as válasz versenye. */
        assert(app_intercom_audio_request(false, 7) == ESP_OK);
        vTaskDelay(80);
    }
    snprintf(response, sizeof(response),
        "SIP/2.0 %s\r\nVia: %s\r\nFrom: %s\r\nTo: %s;tag=door\r\nCall-ID: %s\r\n"
        "CSeq: 1 INVITE\r\nContact: <sip:smarthome@127.0.0.2:5060>\r\nContent-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
        door->mode == 2 ? "488 Not Acceptable Here" : "200 OK", via, from, to, id, body, sdp);
    /* TCP-n egy üzenet több olvasásban is érkezhet. */
    assert(send(fd, response, 17, 0) == 17);
    vTaskDelay(5);
    assert(send(fd, response + 17, strlen(response) - 17, 0) == (int)strlen(response) - 17);
    buffer->used = 0;
    int64_t deadline = esp_timer_get_time() + 3000000;
    while (esp_timer_get_time() < deadline) {
        fd_set set;
        FD_ZERO(&set); FD_SET(fd, &set); FD_SET(door->media, &set);
        struct timeval wait = {.tv_usec=20000};
        int maximum = fd > door->media ? fd : door->media;
        int selected = select(maximum + 1, &set, NULL, NULL, &wait);
        if (selected > 0 && FD_ISSET(fd, &set)) {
            int count = recv(fd, buffer->data + buffer->used, INTERCOM_SIP_MAX - buffer->used, 0);
            if (count <= 0) break;
            buffer->used += (size_t)count;
            buffer->data[buffer->used] = 0;
            int size;
            while ((size = intercom_sip_size(buffer->data, buffer->used)) > 0) {
                if (!strncmp(buffer->data, "ACK ", 4)) door->ack = true;
                if (!strncmp(buffer->data, "BYE ", 4)) door->bye = true;
                if (!strncmp(buffer->data, "CANCEL ", 7)) door->cancelled = true;
                memmove(buffer->data, buffer->data + size, buffer->used - (size_t)size);
                buffer->used -= (size_t)size;
                buffer->data[buffer->used] = 0;
            }
            if (door->bye || (door->mode == 2 && door->ack)) break;
        }
        if (selected > 0 && FD_ISSET(door->media, &set)) {
            uint8_t packet[172];
            int count = recv(door->media, packet, sizeof(packet), 0);
            assert(count == 172 && (packet[1] & 127) == 0);
            assert(abs(intercom_g711_decode(packet[12], 0) - 4000) < 200);
            door->packets++;
        }
        if (door->mode == 4 && door->packets >= 2 && !door->sent_bye) {
            char bye[2048];
            snprintf(bye, sizeof(bye),
                     "BYE sip:display@127.0.0.1:5060 SIP/2.0\r\nVia: SIP/2.0/UDP 127.0.0.2;branch=z9hG4bKdoor\r\n"
                     "From: %s;tag=door\r\nTo: %s\r\nCall-ID: %s\r\nCSeq: 3 BYE\r\nContent-Length: 0\r\n\r\n",
                     to, from, id);
            struct sockaddr_in sip_destination = destination;
            sip_destination.sin_port = htons(5060);
            assert(sendto(door->sip, bye, strlen(bye), 0, (struct sockaddr *)&sip_destination,
                          sizeof(sip_destination)) == (int)strlen(bye));
            door->sent_bye = true;
        }
        if ((!door->mode || door->mode == 4) && door->ack) {
            int16_t samples[160];
            for (unsigned i = 0; i < 160; i++) samples[i] = 2000;
            uint8_t packet[172];
            intercom_rtp_encode(packet, 1, door->packets * 160, 99, 0, samples);
            (void)sendto(door->media, packet, sizeof(packet), 0, (struct sockaddr *)&destination, sizeof(destination));
        }
    }
    close(fd);
    free(buffer);
    return NULL;
}

int main(void)
{
    assert(app_intercom_audio_start() == ESP_OK);
    assert(tasks_created == 0 && !s_running);
    task_failure = true;
    assert(app_intercom_audio_request(true, 7) == ESP_ERR_NO_MEM);
    assert(!s_running && !s_requested && s_status.state == APP_INTERCOM_AUDIO_ERROR);
    task_failure = false;
    for (int mode = 0; mode < 5; mode++) {
        fake_door_t door = {.mode=mode};
        door.listener = socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        setsockopt(door.listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in address = {.sin_family=AF_INET,.sin_port=htons(5060)};
        inet_pton(AF_INET, "127.0.0.2", &address.sin_addr);
        assert(bind(door.listener, (struct sockaddr *)&address, sizeof(address)) == 0);
        assert(listen(door.listener, 1) == 0);
        door.media = socket(AF_INET, SOCK_DGRAM, 0);
        address.sin_port = 0;
        assert(bind(door.media, (struct sockaddr *)&address, sizeof(address)) == 0);
        door.sip = socket(AF_INET, SOCK_DGRAM, 0);
        assert(bind(door.sip, (struct sockaddr *)&address, sizeof(address)) == 0);
        atomic_store(&ring, true);
        s_running = false; s_requested = false;
        assert(app_intercom_audio_request(true, 6) == ESP_ERR_INVALID_STATE);
        assert(app_intercom_audio_request(true, 7) == ESP_OK);
        assert(s_running);
        assert(app_intercom_audio_request(true, 7) == ESP_ERR_INVALID_STATE);
        reads = writes = mic_opens = mic_closes = speaker_closes = 0;
        fail_microphone = mode == 3;
        speaker_audio = false;
        pthread_t thread;
        assert(pthread_create(&thread, NULL, fake_door, &door) == 0);
        audio_worker(NULL);
        assert(!s_running && !s_requested && tasks_created == tasks_deleted);
        pthread_join(thread, NULL);
        close(door.media); close(door.listener); close(door.sip);
        assert(door.ack);
        if (!mode) {
            assert(door.packets && writes == 5 && reads == 5 && speaker_audio);
            assert(mic_opens == 1 && mic_closes == 1 && speaker_closes == 1 && muted);
            assert(door.bye);
        } else if (mode == 4) {
            assert(door.sent_bye && !door.bye && reads && writes && writes < 5);
            assert(mic_opens == 1 && mic_closes == 1 && speaker_closes == 1 && muted);
        } else {
            assert(!mic_opens && !reads && !writes);
            assert(mode == 2 || door.bye);
            if (mode == 1) assert(door.cancelled);
            if (mode == 3) assert(speaker_closes == 1 && mic_closes == 1 && muted);
        }
    }
    puts("PASS: real SIP TCP fragmentation/ACK, bidirectional RTP, explicit bell close, CANCEL/200 race, rejected call without microphone, hardware failure cleanup and repeated calls");
    return 0;
}
