/* A SIP/RTP határok és a G.711 kodek hálózat és hardver nélkül is tesztelhetők.
 * Minden puffer korlátos; a hibás csomag nem mozdítja el a lejátszási órát. */
#include "app_intercom_audio_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool copy_value(char *out, size_t capacity, const char *value, size_t length)
{
    if (!capacity || length >= capacity) {
        return false;
    }
    memcpy(out, value, length);
    out[length] = 0;
    return true;
}

/* Hossz szerint járjuk be a fejléceket: nincs 6 KiB-os veremmásolat a SIP
 * méretellenőrzés minden hívásakor, és nem olvasunk túl a kapott TCP-részleten. */
static bool sip_header_span(const char *message, size_t length, const char *name, char *out, size_t size)
{
    size_t position = 0, key = strlen(name);
    bool first = true, found = false;
    while (position < length) {
        size_t end = position;
        while (end + 1 < length && (message[end] != '\r' || message[end + 1] != '\n'))
            end++;
        if (end + 1 >= length)
            return false;
        if (end == position)
            return found;
        const char *line = message + position;
        if (!first && end - position > key && !strncasecmp(line, name, key) && line[key] == ':') {
            const char *value = line + key + 1;
            while (value < message + end && (*value == ' ' || *value == '\t'))
                value++;
            if (found || !copy_value(out, size, value, (size_t)(message + end - value)))
                return false;
            found = true;
        }
        first = false;
        position = end + 2;
    }
    return found;
}

bool intercom_sip_header(const char *message, const char *name, char *out, size_t size)
{
    return sip_header_span(message, strlen(message), name, out, size);
}

int intercom_sip_size(const char *data, size_t size)
{
    if (size > INTERCOM_SIP_MAX || memchr(data, 0, size)) {
        return -1;
    }
    size_t header = 0;
    for (size_t i = 3; i < size; i++) {
        if (!memcmp(data + i - 3, "\r\n\r\n", 4)) {
            header = i + 1;
            break;
        }
    }
    if (!header) {
        return size == INTERCOM_SIP_MAX ? -1 : 0;
    }
    char value[24];
    /* TCP-n a hosszt mindig meg kell adni, az üres törzshöz is. */
    if (!sip_header_span(data, header, "Content-Length", value, sizeof(value)) || !value[0]) {
        return -1;
    }
    size_t body = 0;
    for (const char *p = value; *p; p++) {
        if (!isdigit((unsigned char)*p) || body > INTERCOM_SIP_MAX / 10) {
            return -1;
        }
        body = body * 10 + (unsigned)(*p - '0');
    }
    if (body > INTERCOM_SIP_MAX - header) {
        return -1;
    }
    return size >= header + body ? (int)(header + body) : 0;
}

bool intercom_audio_host(const char *url, char *host, size_t size)
{
    if (strncmp(url, "http://", 7)) {
        return false;
    }
    const char *start = url + 7;
    const char *end = start;
    while (*end && *end != '/' && *end != ':') {
        if (!isalnum((unsigned char)*end) && *end != '-' && *end != '.') {
            return false;
        }
        end++;
    }
    if (end == start) {
        return false;
    }
    const char *tail = end;
    if (*tail == ':') {
        tail++;
        unsigned port = 0;
        if (!isdigit((unsigned char)*tail)) {
            return false;
        }
        while (isdigit((unsigned char)*tail)) {
            port = port * 10 + (unsigned)(*tail++ - '0');
            if (port > 65535) {
                return false;
            }
        }
        if (!port) {
            return false;
        }
    }
    if (*tail && *tail != '/') {
        return false;
    }
    return copy_value(host, size, start, (size_t)(end - start));
}

bool intercom_sdp_audio(const char *sdp, const char *peer, uint16_t *port, uint8_t *payload)
{
    char session_ip[64] = "", media_ip[64] = "";
    unsigned chosen_port = 0;
    bool audio = false, seen_media = false, found = false, can_send = true;
    uint8_t chosen_payload = 255;
    for (const char *line = sdp; *line;) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length && line[length - 1] == '\r') {
            length--;
        }
        char value[384];
        if (!copy_value(value, sizeof(value), line, length)) {
            return false;
        }
        if (!strncmp(value, "m=", 2)) {
            if (found) {
                break;
            }
            seen_media = true;
            audio = !strncmp(value, "m=audio ", 8);
            if (audio) {
                char *cursor = value + 8, *after = NULL;
                unsigned long number = strtoul(cursor, &after, 10);
                if (after == cursor || strncmp(after, " RTP/AVP ", 9) || !number || number > 65534) {
                    return false;
                }
                chosen_port = (unsigned)number;
                cursor = after + 9;
                while (*cursor) {
                    number = strtoul(cursor, &after, 10);
                    if (after == cursor || (*after && *after != ' ')) {
                        return false;
                    }
                    if (chosen_payload == 255 && (number == 0 || number == 8)) {
                        chosen_payload = (uint8_t)number;
                    }
                    cursor = after;
                    while (*cursor == ' ') {
                        cursor++;
                    }
                }
                found = true;
            }
        } else if (!strncmp(value, "c=IN IP4 ", 9) && (!seen_media || audio)) {
            if (!copy_value(seen_media ? media_ip : session_ip, 64, value + 9, strlen(value + 9))) {
                return false;
            }
        } else if ((!seen_media || audio) &&
                   (!strcmp(value, "a=inactive") || !strcmp(value, "a=sendonly") || !strcmp(value, "a=recvonly"))) {
            can_send = false;
        } else if (audio && (!strncmp(value, "a=rtpmap:0 ", 11) || !strncmp(value, "a=rtpmap:8 ", 11))) {
            const char *expected = value[9] == '0' ? "PCMU/8000" : "PCMA/8000";
            if (strcasecmp(value + 11, expected) &&
                !(strlen(value + 11) == strlen(expected) + 2 &&
                  !strncasecmp(value + 11, expected, strlen(expected)) &&
                  !strcmp(value + 11 + strlen(expected), "/1"))) {
                return false;
            }
        }
        line = end ? end + 1 : line + strlen(line);
    }
    if (!found || !can_send || chosen_payload == 255 ||
        strcmp(media_ip[0] ? media_ip : session_ip, peer)) {
        return false;
    }
    *port = (uint16_t)chosen_port;
    *payload = chosen_payload;
    return true;
}

uint8_t intercom_g711_encode(int16_t sample, uint8_t payload)
{
    int pcm = sample;
    unsigned mask, segment = 0;
    if (payload == 8) {
        pcm >>= 3;
        mask = pcm >= 0 ? 0xd5 : 0x55;
        if (pcm < 0) {
            pcm = -pcm - 1;
        }
        unsigned limit = 0x1f;
        while (segment < 8 && (unsigned)pcm > limit) {
            segment++;
            limit = (limit << 1) | 1;
        }
        if (segment >= 8) {
            return (uint8_t)(0x7f ^ mask);
        }
        unsigned value = segment << 4;
        value |= (unsigned)(pcm >> (segment < 2 ? 1 : segment)) & 15;
        return (uint8_t)(value ^ mask);
    }
    mask = pcm < 0 ? 0x7f : 0xff;
    if (pcm < 0) {
        pcm = -pcm;
    }
    if (pcm > 32635) {
        pcm = 32635;
    }
    pcm += 0x84;
    for (unsigned limit = 0xff; segment < 8 && (unsigned)pcm > limit; limit = (limit << 1) | 1) {
        segment++;
    }
    return (uint8_t)(((segment << 4) | (((unsigned)pcm >> (segment + 3)) & 15)) ^ mask);
}

int16_t intercom_g711_decode(uint8_t sample, uint8_t payload)
{
    if (payload == 8) {
        unsigned value = sample ^ 0x55;
        int pcm = (int)(value & 15) << 4;
        unsigned segment = (value & 0x70) >> 4;
        pcm += segment ? 0x108 : 8;
        if (segment > 1) {
            pcm <<= segment - 1;
        }
        return (int16_t)((value & 0x80) ? pcm : -pcm);
    }
    unsigned value = (uint8_t)~sample;
    int pcm = (((int)(value & 15) << 3) + 0x84) << ((value & 0x70) >> 4);
    return (int16_t)((value & 0x80) ? 0x84 - pcm : pcm - 0x84);
}

static uint32_t read32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

size_t intercom_rtp_encode(uint8_t *packet, uint16_t sequence, uint32_t timestamp,
                          uint32_t ssrc, uint8_t payload, const int16_t samples[160])
{
    packet[0] = 0x80;
    packet[1] = payload;
    packet[2] = (uint8_t)(sequence >> 8);
    packet[3] = (uint8_t)sequence;
    write32(packet + 4, timestamp);
    write32(packet + 8, ssrc);
    for (size_t i = 0; i < 160; i++) {
        packet[12 + i] = intercom_g711_encode(samples[i], payload);
    }
    return 172;
}

bool intercom_rtp_receive(intercom_jitter_t *jitter, const uint8_t *packet, size_t size, uint8_t payload)
{
    if (size < 12 || packet[0] >> 6 != 2 || (packet[1] & 127) != payload) {
        return false;
    }
    size_t offset = 12 + (packet[0] & 15) * 4;
    if (offset > size) {
        return false;
    }
    if (packet[0] & 0x10) {
        if (size - offset < 4) {
            return false;
        }
        size_t extension = 4 + (((size_t)packet[offset + 2] << 8) | packet[offset + 3]) * 4;
        if (extension > size - offset) {
            return false;
        }
        offset += extension;
    }
    if (packet[0] & 0x20) {
        unsigned padding = packet[size - 1];
        if (!padding || padding > size - offset) {
            return false;
        }
        size -= padding;
    }
    size_t count = size - offset;
    if (!count || count > 960) {
        return false;
    }
    uint32_t timestamp = read32(packet + 4), ssrc = read32(packet + 8);
    if (jitter->initialized && (ssrc != jitter->ssrc ||
        (int32_t)(timestamp - jitter->next) < 0 ||
        (uint32_t)(timestamp - jitter->next) + count > INTERCOM_JITTER_SAMPLES)) {
        return false;
    }
    if (!jitter->initialized) {
        jitter->initialized = true;
        jitter->ssrc = ssrc;
        jitter->next = timestamp;
        jitter->priming = 2;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t time = timestamp + (uint32_t)i;
        unsigned slot = time % INTERCOM_JITTER_SAMPLES;
        jitter->samples[slot] = intercom_g711_decode(packet[offset + i], payload);
        jitter->tags[slot] = time;
        jitter->valid[slot] = 1;
    }
    return true;
}

void intercom_jitter_read(intercom_jitter_t *jitter, int16_t *out, size_t samples)
{
    memset(out, 0, samples * sizeof(*out));
    if (!jitter->initialized) {
        return;
    }
    if (jitter->priming) {
        jitter->priming--;
        return;
    }
    jitter->playing = true;
    for (size_t i = 0; i < samples; i++, jitter->next++) {
        unsigned slot = jitter->next % INTERCOM_JITTER_SAMPLES;
        if (jitter->valid[slot] && jitter->tags[slot] == jitter->next) {
            out[i] = jitter->samples[slot];
            jitter->valid[slot] = 0;
        }
    }
}
