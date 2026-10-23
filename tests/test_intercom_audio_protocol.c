/* Valódi SIP/SDP/RTP parser és kodek: töredezés, rossz forrás, túlcsordulás. */
#include "app_intercom_audio_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
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
