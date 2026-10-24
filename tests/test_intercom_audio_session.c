/* A valódi audio worker helyi TCP/UDP kaputelefonnal; csak az I2S/AEC hardver hamis. */
#include <assert.h>
#include <setjmp.h>
static jmp_buf worker_exit;
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
static bool gate_timeout_mode;
static bool tone_mode, tone_ring_stop;
static int device_volume;
static unsigned aec_creates;
static unsigned full_snapshots, tone_first_snapshots;
static atomic_llong clock_offset;
static unsigned test_frame = 128;
static bool fail_aec, fail_aec_inner;

void vTaskDelay(unsigned delay)
{
    struct timespec time = {.tv_sec=delay / 1000, .tv_nsec=(long)(delay % 1000) * 1000000L};
    nanosleep(&time, NULL);
}
int xTaskCreatePinnedToCoreWithCaps(void (*worker)(void *), const char *name, unsigned stack,
                           void *argument, unsigned priority, void *handle, unsigned core, unsigned caps)
{
    assert(worker && !strcmp(name, "intercom_audio") && stack == 16384 &&
           priority == 7 && core == 0);
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    (void)argument;
    assert(handle);
    if (task_failure) return 0;
    *(TaskHandle_t *)handle = (void *)1;
    tasks_created++;
    return pdPASS;
}
void vTaskSuspend(TaskHandle_t task)
{
    assert(!task);
    longjmp(worker_exit, 1);
}
void vTaskDeleteWithCaps(void *task)
{
    assert(task == (void *)1);
    tasks_deleted++;
}
static int64_t real_time_us(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (int64_t)time.tv_sec * 1000000LL + time.tv_nsec / 1000;
}
int64_t esp_timer_get_time(void) { return real_time_us() + atomic_load(&clock_offset); }
uint32_t esp_random(void)
{
    static uint32_t value = 128;
    return ++value;
}
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    void *memory = NULL;
    if (posix_memalign(&memory, alignment, count * size) != 0) return NULL;
    memset(memory, 0, count * size);
    return memory;
}
esp_err_t bsp_i2c_init(void) { return ESP_OK; }
static int i2s_inits;
static bool fail_i2s;
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 8192; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 4096; }
esp_err_t bsp_audio_init(const void *configuration)
{
    i2s_inits++;
    const i2s_std_config_t *config = configuration;
    assert(config && config->clk_cfg.sample_rate == 16000);
    assert(config->slot_cfg.bits == 16 && config->slot_cfg.mode == 2);
    assert(config->gpio_cfg.mclk == 13 && config->gpio_cfg.bclk == 12 &&
           config->gpio_cfg.ws == 10 && config->gpio_cfg.dout == 9 && config->gpio_cfg.din == 11);
    return fail_i2s ? ESP_ERR_NO_MEM : ESP_OK;
}
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
    assert(handle == (void *)1 && volume >= 0 && volume <= 100);
    device_volume = volume;
    return 0;
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
    assert(handle == (void *)2 && gain == 31.5f); return 0;
}
int esp_codec_dev_read(esp_codec_dev_handle_t handle, void *data, int size)
{
    assert(handle == (void *)2 && size == (int)test_frame * 8);
    assert(writes == reads + 1); /* A lejátszás megelőzi az ADC/DSP lépést. */
    for (int i = 0; i < size / 4; i++) {
        ((int16_t *)data)[i * 2] = i % 2 ? 5000 : 3000;
        ((int16_t *)data)[i * 2 + 1] = -12000;
    }
    reads++;
    vTaskDelay(32);
    return 0;
}
int esp_codec_dev_write(esp_codec_dev_handle_t handle, void *data, int size)
{
    assert(handle == (void *)1 && data && muted == (device_volume == 0));
    if (tone_mode) {
        if (!writes) tone_first_snapshots = full_snapshots;
        else assert(full_snapshots == tone_first_snapshots);
        assert(size == 1280 && reads == 0 && device_volume == 71);
        int16_t *pcm = data;
        assert(pcm[0] == 0 && pcm[8] == 8192 && pcm[24] == -8192);
        for (int i = 0; i < size / 4; i++) assert(pcm[i * 2] == pcm[i * 2 + 1]);
        if (++writes == 3) {
            if (tone_ring_stop) atomic_store(&ring, true);
            else assert(app_intercom_audio_test_request(false, 0) == ESP_OK);
        }
        /* A driver által módosított puffer sem halkíthatja el a következő periódust. */
        memset(data, 0, (size_t)size);
        return 0;
    }
    assert(size == (int)test_frame * 8);
    assert(device_volume == 73);
    for (int i = 0; i < size / 2; i++) {
        int16_t sample = ((int16_t *)data)[i];
        assert(sample == 0 || abs(sample - 2000) < 100);
        assert(sample == ((int16_t *)data)[i ^ 1]);
        if (abs(sample) > 100) speaker_audio = true;
    }
    writes++;
    if (writes == 3) {
        atomic_store(&ring, false);
        assert(app_intercom_audio_active_for(7) && wanted(s_epoch, 7));
        if (gate_timeout_mode) app_intercom_audio_gate_opened(7);
    }
    if (writes == 5) {
        if (gate_timeout_mode) atomic_store(&clock_offset, 5100000);
        else assert(app_intercom_audio_request(false, 7) == ESP_OK);
    }
    return 0;
}
aec_handle_t *aec_create_from_config(aec_config_t *config)
{
    aec_creates++;
    assert(config->sample_rate == 8000 && config->filter_length == 4 &&
           config->mic_num == 1 && config->ref_num == 1 && config->out_num == 1 &&
           config->mode == AEC_MODE_VOIP_LOW_COST && config->nlp_level == AEC_NLP_LEVEL_AGGR && config->caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_aec) return NULL;
    aec_handle_t *handle = calloc(1, sizeof(*handle));
    if (handle) {
        handle->frame_size = test_frame;
        handle->aec_handle = fail_aec_inner ? NULL : (void *)1;
    }
    return handle;
}
int aec_get_chunksize(aec_handle_t *aec) { return aec->frame_size; }
void aec_process(aec_handle_t *aec, int16_t *input, int16_t *reference, int16_t *output)
{
    assert(aec && input && reference && output);
    for (unsigned i = 0; i < test_frame; i++) assert(input[i] == 4000);
    memcpy(output, input, test_frame * 2);
}
void aec_destroy(aec_handle_t *aec) { free(aec); }
bool app_loxone_intercom_test_permitted(unsigned revision)
{
    return !atomic_load(&ring) && revision == 7;
}
bool app_loxone_intercom_call_permitted(unsigned revision, bool active)
{
    return revision == 7 && (atomic_load(&ring) || active);
}
bool app_loxone_intercom_snapshot(app_intercom_snapshot_t *out)
{
    full_snapshots++;
    *out = (app_intercom_snapshot_t){.ringing=atomic_load(&ring),.connected=true,.revision=7};
    out->config.enabled = true;
    out->config.speaker_volume = 73;
    out->config.microphone_gain = 84;
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
    int body = snprintf(sdp, sizeof(sdp), "v=0\r\nc=IN IP4 127.0.0.2\r\nm=audio %u RTP/AVP %s\r\na=sendrecv\r\n",
                        ntohs(local.sin_port), door->mode == 5 ? "0 8" : "0");
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
    int64_t deadline = real_time_us() + 3000000;
    uint32_t last_timestamp = 0;
    while (real_time_us() < deadline) {
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
            uint32_t timestamp;
            memcpy(&timestamp, packet + 4, sizeof(timestamp));
            timestamp = ntohl(timestamp);
            if (door->packets) assert(timestamp - last_timestamp == 160);
            last_timestamp = timestamp;
            for (unsigned i = 12; i < sizeof(packet); i++)
                assert(abs(intercom_g711_decode(packet[i], 0) - 4000) < 200);
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
        if ((!door->mode || door->mode == 4 || door->mode == 5) && door->ack) {
            int16_t samples[160];
            for (unsigned i = 0; i < 160; i++) samples[i] = 2000;
            uint8_t packet[172];
            intercom_rtp_encode(packet, 1, door->packets * 160, 99, door->mode == 5 ? 8 : 0, samples);
            /* A küldőport eltér az SDP fogadóportjától; a visszaút PCMA,
             * miközben a kijelző a preferált PCMU kodekkel küld. */
            (void)sendto(door->mode == 5 ? door->sip : door->media, packet, sizeof(packet), 0,
                         (struct sockaddr *)&destination, sizeof(destination));
        }
    }
    close(fd);
    free(buffer);
    return NULL;
}

int main(void)
{
    /* Csend, szélsőérték és sztereó csatornák külön mérése. */
    const int16_t levels[] = {0, -32768, 16384, 0};
    uint16_t peak = 0;
    measure_signal(&peak, levels, 2, 2);
    assert(peak == 50);
    peak = 0;
    measure_signal(&peak, levels + 1, 2, 2);
    assert(peak == 100);

    assert(app_intercom_audio_start() == ESP_OK);
    s_requested = true;
    s_revision = 7;
    s_status.state = APP_INTERCOM_AUDIO_ACTIVE;
    atomic_store(&ring, false);
    assert(app_intercom_audio_active_for(7) && wanted(s_epoch, 7));
    assert(!app_intercom_audio_active_for(6));
    app_intercom_audio_gate_opened(6);
    assert(!s_gate_stop_at);
    app_intercom_audio_gate_opened(7);
    int64_t deadline = s_gate_stop_at;
    app_intercom_audio_gate_opened(7);
    assert(s_gate_stop_at == deadline);
    atomic_store(&clock_offset, 4900000);
    assert(app_intercom_audio_active_for(7) && wanted(s_epoch, 7));
    atomic_store(&clock_offset, 5100000);
    assert(!app_intercom_audio_active_for(7) && !wanted(s_epoch, 7));
    atomic_store(&clock_offset, 0);
    s_gate_stop_at = 0;
    s_status.state = APP_INTERCOM_AUDIO_CONNECTING;
    assert(!wanted(s_epoch, 7));
    s_requested = false;
    s_status.state = APP_INTERCOM_AUDIO_IDLE;
    atomic_store(&ring, true);
    assert(i2s_inits == 1 && mic_opens == 0);
    assert(tasks_created == 0 && !s_running && !app_intercom_audio_busy());
    task_failure = true;
    assert(app_intercom_audio_request(true, 7) == ESP_ERR_NO_MEM);
    assert(!s_running && !s_requested && s_status.state == APP_INTERCOM_AUDIO_ERROR);
    task_failure = false;
    /* Az algoritmus keretmérete eltérhet; mindhárommal végigfut a tényleges RTP-út. */
    for (unsigned variant = 0; variant < 3; variant++) {
    test_frame = 128U << variant;
    for (int mode = 0; mode < 6; mode++) {
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
        assert(s_running && app_intercom_audio_busy());
        assert(app_intercom_audio_request(true, 7) == ESP_ERR_INVALID_STATE);
        reads = writes = mic_opens = mic_closes = speaker_closes = 0;
        fail_microphone = mode == 3;
        gate_timeout_mode = mode == 5;
        speaker_audio = false;
        pthread_t thread;
        assert(pthread_create(&thread, NULL, fake_door, &door) == 0);
        if (!setjmp(worker_exit)) audio_worker(NULL);
        atomic_store(&clock_offset, 0);
        assert(s_running && s_audio_task.finished);
        assert(!app_intercom_audio_busy());
        assert(!s_running && !app_intercom_audio_busy() && !s_requested && tasks_created == tasks_deleted);
        pthread_join(thread, NULL);
        close(door.media); close(door.listener); close(door.sip);
        assert(door.ack);
        if (!mode || mode == 5) {
            assert(door.packets && writes == 5 && reads == 4 && speaker_audio);
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
    }
    /* Csak a megfelelő IP, az első érvényes küldőport és a tárgyalt kodek
     * engedélyezett. Hibás csomag nem rögzítheti a portot. */
    audio_call_t *rx = calloc(1, sizeof(*rx));
    assert(rx);
    inet_pton(AF_INET, "127.0.0.2", &rx->media.sin_addr);
    rx->media.sin_port = htons(5004);
    rx->formats = 3;
    struct sockaddr_in source = {.sin_family=AF_INET, .sin_port=htons(6000)};
    inet_pton(AF_INET, "127.0.0.3", &source.sin_addr);
    uint8_t packet[172];
    int16_t silence[160] = {0};
    intercom_rtp_encode(packet, 1, 0, 22, 8, silence);
    assert(!receive_audio(rx, &source, packet, sizeof(packet)) && !rx->receive_port);
    source.sin_addr = rx->media.sin_addr;
    assert(!receive_audio(rx, &source, packet, 1) && !rx->receive_port);
    assert(receive_audio(rx, &source, packet, sizeof(packet)) && rx->receive_port == source.sin_port);
    source.sin_port = htons(6001);
    assert(!receive_audio(rx, &source, packet, sizeof(packet)));
    source.sin_port = htons(6000);
    intercom_rtp_encode(packet, 2, 160, 22, 0, silence);
    rx->formats = 2;
    assert(!receive_audio(rx, &source, packet, sizeof(packet)));
    rx->formats = 3;
    packet[8] ^= 1;
    assert(!receive_audio(rx, &source, packet, sizeof(packet)));
    assert(rx->rx_packets == 6 && rx->rx_accepted == 1 && rx->rx_peer_rejected == 2 &&
           rx->rx_format_rejected == 2 && rx->rx_invalid == 1);
    free(rx);
    atomic_store(&ring, true);
    assert(app_intercom_audio_request(true, 7) == ESP_OK);
    audio_call_t *failure = calloc(1, sizeof(*failure));
    assert(failure);
    failure->epoch = s_epoch;
    failure->revision = 7;
    fail_aec = true;
    assert(!open_hardware(failure) && strstr(failure->hardware_error, "Visszhangszűrő memória"));
    fail_aec = false;
    fail_aec_inner = true;
    assert(!open_hardware(failure) && strstr(failure->hardware_error, "Visszhangszűrő memória"));
    aec_destroy(failure->aec); failure->aec = NULL;
    fail_aec_inner = false;
    test_frame = 0;
    assert(!open_hardware(failure) && strstr(failure->hardware_error, "keretméret"));
    aec_destroy(failure->aec);
    free(failure);
    s_running = s_requested = false;
    assert(i2s_inits == 1);
    /* Részleges BSP-hiba után soha nem használjuk újra a törölt csatornákat. */
    /* A teszthang végigmegy a tényleges worker/kodek útvonalon, hálózat és ADC nélkül. */
    tone_mode = true;
    for (volatile unsigned stop_mode = 0; stop_mode < 2; stop_mode++) {
        tone_ring_stop = stop_mode != 0;
        reads = writes = mic_opens = mic_closes = speaker_closes = 0;
        atomic_store(&ring, false);
        unsigned aec_before = aec_creates;
        assert(app_intercom_audio_test_request(true, 101) == ESP_ERR_INVALID_ARG);
        assert(app_intercom_audio_test_request(true, 71) == ESP_OK);
        assert(app_intercom_audio_test_request(true, 71) == ESP_ERR_INVALID_STATE);
        if (!setjmp(worker_exit)) audio_worker(NULL);
        assert(!app_intercom_audio_busy());
        assert(writes == 3 && !reads && !mic_opens && !mic_closes && speaker_closes == 1 && muted);
        assert(aec_creates == aec_before);
    }
    tone_mode = false;
    s_i2s_attempted = false;
    fail_i2s = true;
    audio_call_t *failed_i2s = calloc(1, sizeof(*failed_i2s));
    assert(failed_i2s && !open_hardware(failed_i2s));
    assert(strstr(failed_i2s->hardware_error, "DMA: 8192 B szabad, 4096 B") && i2s_inits == 2);
    assert(!open_hardware(failed_i2s) && i2s_inits == 2);
    free(failed_i2s);
    puts("PASS: real SIP/RTP session, 8 kHz AEC with 16 kHz stereo I2S, playback before ADC/DSP, "
         "left-channel decimation, 20 ms RTP clock, configured microphone gain, "
         "128/256/512-sample AEC, bell hold, timed/manual close, tone isolation and cleanup");
    return 0;
}
