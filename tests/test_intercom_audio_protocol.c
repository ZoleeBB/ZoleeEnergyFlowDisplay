/* Valódi SIP/SDP/RTP parser és kodek: töredezés, rossz forrás, túlcsordulás. */
#include "app_intercom_audio_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void jitter_network_delay(void)
{
    intercom_jitter_t *jitter = calloc(1, sizeof(*jitter));
    assert(jitter);
    uint8_t packet[172];
    int16_t input[160], output[512];
    for (unsigned i = 0; i < 160; i++) input[i] = 3000;
    const unsigned blocks[] = {128, 160, 256, 512};
    for (unsigned variant = 0; variant < 4; variant++) {
        memset(jitter, 0, sizeof(*jitter));
        bool sent[120] = {0};
        unsigned frame = blocks[variant], played = 0;
        /* 20 ms-os RTP; 16/20 ms-os lejátszásnál minden ötödik csomag 40 ms-ot
         * késik. Nagyobb blokknál a daraboláson átnyúló 60 ms-os indulást mérjük. */
        for (unsigned now = 0; now < 16000; now += frame) {
            for (unsigned sequence = 0; sequence < 120; sequence++) {
                unsigned due = sequence * 160 + (frame <= 160 && sequence % 5 == 1 ? 320 : 0);
                if (!sent[sequence] && due <= now) {
                    intercom_rtp_encode(packet, (uint16_t)sequence, sequence * 160, 44, 0, input);
                    assert(intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
                    sent[sequence] = true;
                }
            }
            intercom_jitter_read(jitter, output, frame);
            for (unsigned i = 0; i < frame; i++) {
                if (now + i < INTERCOM_JITTER_PRIME_SAMPLES) assert(output[i] == 0);
                else { assert(output[i] > 2000); played++; }
            }
        }
        assert(played > 15000 && !jitter->resyncs && !jitter->late_packets);
    }
    free(jitter);
    puts("PASS: constant 60ms priming at four DSP block sizes; reordered 40ms-late RTP at 16/20ms playout");
}

static void jitter_recovery(void)
{
    intercom_jitter_t *jitter = calloc(1, sizeof(*jitter));
    assert(jitter);
    uint8_t packet[172];
    int16_t samples[160], output[256];
    for (unsigned i = 0; i < 160; i++) samples[i] = 3000;
    /* Típusonként harminc másodpercnyi hálózati hang, eltérő lejátszási órával.
     * A régi kód néhány tucat jó csomag után minden továbbit eldobott. */
    const unsigned reads[] = {128, 160, 192};
    for (unsigned mode = 0; mode < 3; mode++) {
        memset(jitter, 0, sizeof(*jitter));
        unsigned accepted = 0, audible = 0, tail_audible = 0;
        for (unsigned i = 0; i < 1500; i++) {
            intercom_rtp_encode(packet, (uint16_t)(65000 + i), 0xffff0000U + i * 160U, 44, 0, samples);
            accepted += intercom_rtp_receive(jitter, packet, sizeof(packet), 0);
            intercom_jitter_read(jitter, output, reads[mode]);
            for (unsigned j = 0; j < reads[mode]; j++) {
                audible += output[j] > 2000;
                if (i >= 1400) tail_audible += output[j] > 2000;
            }
        }
        assert(accepted > 1400 && audible > 120000 && tail_audible > 8000);
        if (mode != 1) assert(jitter->resyncs > 0);
        else assert(!jitter->resyncs);
    }
    /* Csak a csomag régi elejét vágjuk le, az új végét le lehet játszani. */
    memset(jitter, 0, sizeof(*jitter));
    intercom_rtp_encode(packet, 1, 1000, 44, 0, samples);
    assert(intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    jitter->next = 1080;
    assert(intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    jitter->priming = 0;
    intercom_jitter_read(jitter, output, 80);
    for (unsigned i = 0; i < 80; i++) assert(output[i] > 2000);
    /* Két előreszaladt csomag kell; a régi sorszám, rossz SSRC és hibás
     * fejléc nem szinkronizálhat. A sorszám 65535 -> 0 átfordulása érvényes. */
    memset(jitter, 0, sizeof(*jitter));
    intercom_rtp_encode(packet, 65534, 0, 44, 0, samples);
    assert(intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    intercom_rtp_encode(packet, 65535, 10000, 44, 0, samples);
    assert(!intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    assert(!jitter->resyncs && jitter->next == 0);
    intercom_rtp_encode(packet, 0, 10160, 45, 0, samples);
    assert(!intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    assert(jitter->wrong_ssrc == 1 && !jitter->resyncs);
    intercom_rtp_encode(packet, 0, 10160, 44, 0, samples);
    assert(intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    assert(jitter->resyncs == 1 && jitter->next == 10160);
    intercom_rtp_encode(packet, 65534, 0, 44, 0, samples);
    assert(!intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    assert(!intercom_rtp_receive(jitter, packet, sizeof(packet), 0));
    assert(jitter->resyncs == 1 && jitter->next == 10160);
    assert(!intercom_rtp_receive(jitter, packet, 4, 0) && jitter->malformed == 1);
    free(jitter);
    puts("PASS: 30s slow/fast playout recovery, partial late packet, two-packet resync, sequence/timestamp wrap, stale/foreign/malformed rejection");
}

int main(void)
{
    jitter_network_delay();
    jitter_recovery();
    const char *message = "SIP/2.0 200 OK\r\nCall-ID: test\r\nContent-Length: 4\r\n\r\ntest";
    for (size_t i = 0; i < strlen(message); i++) {
        assert(intercom_sip_size(message, i) == 0);
    }
    assert(intercom_sip_size(message, strlen(message)) == (int)strlen(message));
    char field[8];
    assert(intercom_sip_header(message, "call-id", field, sizeof(field)) && !strcmp(field, "test"));
    assert(!intercom_sip_header(message, "Call-ID", field, 4));
    const char *bad[] = {
        "SIP/2.0 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
        "SIP/2.0 200 OK\r\nContent-Length: -1\r\n\r\n",
        "SIP/2.0 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n",
        "SIP/2.0 200 OK\r\n\r\n",
    };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        assert(intercom_sip_size(bad[i], strlen(bad[i])) == -1);
    }
    char host[128];
    assert(intercom_audio_host("http://intercom.local:80/mjpg/video.mjpg", host, sizeof(host)));
    assert(!strcmp(host, "intercom.local"));
    assert(!intercom_audio_host("http://user:password@192.168.1.4/", host, sizeof(host)));
    assert(!intercom_audio_host("https://192.168.1.4/", host, sizeof(host)));
    assert(!intercom_audio_host("http://bad\r\nhost/", host, sizeof(host)));
    uint16_t port;
    uint8_t payload;
    const char *sdp = "v=0\r\nc=IN IP4 192.168.1.4\r\nm=audio 4000 RTP/AVP 8 0\r\n"
                      "a=rtpmap:8 PCMA/8000\r\na=sendrecv\r\n";
    assert(intercom_sdp_audio(sdp, "192.168.1.4", &port, &payload) && port == 4000 && payload == 8);
    uint8_t formats = 0;
    assert(intercom_sdp_audio_formats(sdp, "192.168.1.4", &port, &payload, &formats) && formats == 3);
    assert(intercom_sdp_audio_formats("c=IN IP4 192.168.1.4\r\nm=audio 4000 RTP/AVP 0\r\n",
                                    "192.168.1.4", &port, &payload, &formats) && formats == 1);
    assert(!intercom_sdp_audio(sdp, "192.168.1.5", &port, &payload));
    assert(!intercom_sdp_audio("m=audio 0 RTP/AVP 0\r\n", "192.168.1.4", &port, &payload));
    assert(!intercom_sdp_audio("c=IN IP4 192.168.1.4\r\nm=audio 4000 RTP/SAVP 0\r\n", "192.168.1.4", &port, &payload));
    assert(!intercom_sdp_audio("c=IN IP4 192.168.1.4\r\nm=audio 4000 RTP/AVP 111\r\n", "192.168.1.4", &port, &payload));
    assert(!intercom_sdp_audio("c=IN IP4 192.168.1.4\r\nm=audio 4000 RTP/AVP 0\r\na=sendonly\r\n", "192.168.1.4", &port, &payload));
    assert(!intercom_sdp_audio("c=IN IP4 192.168.1.4\r\nm=audio 4000 RTP/AVP 0\r\na=rtpmap:0 opus/48000\r\n", "192.168.1.4", &port, &payload));
    assert(intercom_g711_decode(0xff, 0) == 0 && intercom_g711_encode(0, 0) == 0xff);
    assert(intercom_g711_decode(0x80, 0) == 32124 && intercom_g711_decode(0, 0) == -32124);
    assert(intercom_g711_decode(0xd5, 8) == 8 && intercom_g711_encode(0, 8) == 0xd5);
    assert(intercom_g711_decode(0xaa, 8) == 32256 && intercom_g711_decode(0x2a, 8) == -32256);
    for (unsigned codec = 0; codec <= 8; codec += 8) {
        for (int sample = -32768; sample <= 32767; sample++) {
            int decoded = intercom_g711_decode(intercom_g711_encode((int16_t)sample, codec), codec);
            assert(abs(sample - decoded) <= 1024);
        }
        int16_t samples[160], output[160];
        for (unsigned i = 0; i < 160; i++) {
            samples[i] = (int16_t)((int)i * 100 - 8000);
        }
        uint8_t packet[172];
        assert(intercom_rtp_encode(packet, 65535, 0xffffffa0, 44, codec, samples) == sizeof(packet));
        intercom_jitter_t *jitter = calloc(1, sizeof(*jitter));
        assert(jitter && intercom_rtp_receive(jitter, packet, sizeof(packet), codec));
        intercom_jitter_read(jitter, output, 160);
        intercom_jitter_read(jitter, output, 160);
        intercom_jitter_read(jitter, output, 160);
        intercom_jitter_read(jitter, output, 160);
        for (unsigned i = 0; i < 160; i++) {
            assert(abs(output[i] - samples[i]) <= 512);
        }
        assert(!intercom_rtp_receive(jitter, packet, sizeof(packet), codec));
        intercom_jitter_read(jitter, output, 160);
        for (unsigned i = 0; i < 160; i++) {
            assert(output[i] == 0);
        }
        intercom_rtp_encode(packet, 0, 0x100, 44, codec, samples);
        assert(intercom_rtp_receive(jitter, packet, sizeof(packet), codec));
        packet[8] ^= 1;
        assert(!intercom_rtp_receive(jitter, packet, sizeof(packet), codec));
        free(jitter);
    }
    /* RTP header, CSRC, extension és padding minden csonkításnál ellenőrzött. */
    intercom_jitter_t *jitter = calloc(1, sizeof(*jitter));
    uint8_t packet[1200] = {0};
    for (size_t i = 0; i < 12; i++) {
        assert(!intercom_rtp_receive(jitter, packet, i, 0));
    }
    packet[0] = 0x9f;
    packet[1] = 0;
    assert(!intercom_rtp_receive(jitter, packet, 12, 0));
    packet[0] = 0xa0;
    packet[12] = 99;
    assert(!intercom_rtp_receive(jitter, packet, 13, 0));
    packet[0] = 0x90;
    packet[14] = packet[15] = 255;
    assert(!intercom_rtp_receive(jitter, packet, 16, 0));
    /* Véletlen bemenetek a valódi parseren sanitizer alatt. */
    unsigned random = 17;
    for (unsigned i = 0; i < 12000; i++) {
        for (size_t j = 0; j < sizeof(packet); j++) {
            random = random * 1664525U + 1013904223U;
            packet[j] = (uint8_t)(random >> 24);
        }
        memset(jitter, 0, sizeof(*jitter));
        (void)intercom_rtp_receive(jitter, packet, random % sizeof(packet), 0);
    }
    free(jitter);
    puts("PASS: actual bounded SIP framing, SDP peer/duplex/codecs, G.711 exhaustive error bounds, RTP wrap/loss/late/SSRC/extension/padding and malformed packets");
    return 0;
}
