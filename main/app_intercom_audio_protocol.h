#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INTERCOM_SIP_MAX 6144
#define INTERCOM_RTP_SAMPLES 160
#define INTERCOM_JITTER_SAMPLES 2048

typedef struct {
    char data[INTERCOM_SIP_MAX + 1];
    size_t used;
} intercom_sip_buffer_t;

/* 0: hiányos üzenet, -1: hibás/túlméretes, >0: teljes üzenet hossza. */
int intercom_sip_size(const char *data, size_t size);
bool intercom_sip_header(const char *message, const char *name, char *out, size_t size);
bool intercom_audio_host(const char *url, char *host, size_t size);
bool intercom_sdp_audio(const char *sdp, const char *peer, uint16_t *port, uint8_t *payload);
uint8_t intercom_g711_encode(int16_t sample, uint8_t payload);
int16_t intercom_g711_decode(uint8_t sample, uint8_t payload);
size_t intercom_rtp_encode(uint8_t *packet, uint16_t sequence, uint32_t timestamp,
                          uint32_t ssrc, uint8_t payload, const int16_t samples[160]);

typedef struct {
    int16_t samples[INTERCOM_JITTER_SAMPLES];
    uint32_t tags[INTERCOM_JITTER_SAMPLES];
    uint8_t valid[INTERCOM_JITTER_SAMPLES];
    uint32_t next, ssrc;
    unsigned priming;
    bool initialized, playing;
} intercom_jitter_t;
/* Forrásellenőrzés a socketnél; itt RTP-határ, típus, SSRC és időbélyeg ellenőrzés. */
bool intercom_rtp_receive(intercom_jitter_t *jitter, const uint8_t *packet,
                          size_t size, uint8_t payload);
void intercom_jitter_read(intercom_jitter_t *jitter, int16_t *out, size_t samples);
