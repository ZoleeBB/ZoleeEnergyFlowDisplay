/* A BLE/NVS/HTTP nélküli közös modell külön ellenőrizhető. Nem foglal heapet,
 * nem végez I/O-t, és sem jelenlét, sem érintés nem okoz flash-írást. */
#include "app_slave_presence.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(app_slave_presence_table_t) <= 512, "Presence table exceeds RAM budget");
_Static_assert(sizeof(app_slave_presence_receiver_t) <= 64, "Presence receiver exceeds RAM budget");
_Static_assert(APP_SLAVE_PRESENCE_PACKET_SIZE == 20, "Presence packet must fit ATT MTU 23");

static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && text[length])
        ++length;
    return length;
}

static bool name_valid(const char *name)
{
    size_t length = bounded_length(name, APP_SLAVE_PRESENCE_NAME_SIZE);
    if (!length || length == APP_SLAVE_PRESENCE_NAME_SIZE)
        return false;
    bool visible = false;
    for (size_t i = 0; i < length;)
    {
        unsigned first = (unsigned char)name[i++];
        uint32_t code = first;
        unsigned remaining = 0;
        uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf)
        {
            code = first & 0x1f;
            remaining = 1;
            minimum = 0x80;
        }
        else if (first >= 0xe0 && first <= 0xef)
        {
            code = first & 0x0f;
            remaining = 2;
            minimum = 0x800;
        }
        else if (first >= 0xf0 && first <= 0xf4)
        {
            code = first & 7;
            remaining = 3;
            minimum = 0x10000;
        }
        else if (first >= 0x80)
            return false;
        if (remaining > length - i)
            return false;
        for (unsigned j = 0; j < remaining; ++j)
        {
            unsigned next = (unsigned char)name[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3f);
        }
        if ((remaining && code < minimum) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || code < 0x20 ||
            (code >= 0x7f && code <= 0x9f))
            return false;
        visible |= code != 0x20 && code != 0xa0;
    }
    return visible;
}

static bool uuid_valid(const char *uuid)
{
    size_t length = bounded_length(uuid, APP_SLAVE_PRESENCE_UUID_SIZE);
    if (!length)
        return true;
    if (length != 35)
        return false;
    for (unsigned i = 0; i < 35; ++i)
    {
        if (i == 8 || i == 13 || i == 18)
        {
            if (uuid[i] != '-')
                return false;
        }
        else if (!((uuid[i] >= '0' && uuid[i] <= '9') ||
                   (uuid[i] >= 'a' && uuid[i] <= 'f')))
            return false;
    }
    return true;
}

bool app_slave_identity_valid(const app_slave_identity_t *identity)
{
    if (!identity || identity->type > 1)
        return false;
    unsigned any = 0, all = 0xff;
    for (unsigned i = 0; i < 6; ++i)
    {
        any |= identity->address[i];
        all &= identity->address[i];
    }
    if (!any || all == 0xff)
        return false;
    if (identity->type == 0)
        return true;
    if ((identity->address[5] & 0xc0) != 0xc0)
        return false;
    /* A static cím alsó 46 bitje sem lehet csupa nulla vagy egy. */
    any = identity->address[5] & 0x3f;
    all = identity->address[5] | 0xc0;
    for (unsigned i = 0; i < 5; ++i)
    {
        any |= identity->address[i];
        all &= identity->address[i];
    }
    return any != 0 && all != 0xff;
}

bool app_slave_identity_id(const app_slave_identity_t *identity,
                           char out[APP_SLAVE_PRESENCE_ID_SIZE])
{
    if (!out || !app_slave_identity_valid(identity))
        return false;
    int length = snprintf(out, APP_SLAVE_PRESENCE_ID_SIZE,
                          "ble-%u-%02x%02x%02x%02x%02x%02x",
                          (unsigned)identity->type,
                          (unsigned)identity->address[5], (unsigned)identity->address[4],
                          (unsigned)identity->address[3], (unsigned)identity->address[2],
                          (unsigned)identity->address[1], (unsigned)identity->address[0]);
    return length == APP_SLAVE_PRESENCE_ID_SIZE - 1;
}

bool app_slave_presence_profile_valid(const app_slave_presence_profile_t *profile)
{
    return profile && name_valid(profile->name) && uuid_valid(profile->presence_uuid) &&
           profile->timeout_seconds >= 1 && profile->timeout_seconds <= 86400;
}

int app_slave_presence_find(const app_slave_presence_table_t *table,
                            const app_slave_identity_t *identity)
{
    if (!table || !app_slave_identity_valid(identity))
        return -1;
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        const app_slave_presence_peer_t *peer = &table->peers[i];
        if (peer->used && peer->identity.type == identity->type &&
            memcmp(peer->identity.address, identity->address, 6) == 0)
            return (int)i;
    }
    return -1;
}

int app_slave_presence_register(app_slave_presence_table_t *table,
                                const app_slave_identity_t *identity,
                                const char *name)
{
    if (!table || !name || !app_slave_identity_valid(identity))
        return -1;
    int index = app_slave_presence_find(table, identity);
    if (index >= 0)
        return index;
    size_t length = bounded_length(name, APP_SLAVE_PRESENCE_NAME_SIZE);
    if (length == APP_SLAVE_PRESENCE_NAME_SIZE || !name_valid(name))
        return -1;
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        if (!table->peers[i].used)
        {
            app_slave_presence_peer_t peer = {
                .used = true,
                .identity = *identity,
                .profile = {.timeout_seconds = 30},
                .revision = 1,
            };
            memcpy(peer.profile.name, name, length);
            table->peers[i] = peer;
            return (int)i;
        }
    }
    return -1;
}

static uint32_t next_revision(uint32_t revision)
{
    ++revision;
    return revision ? revision : 1;
}

bool app_slave_presence_update(app_slave_presence_table_t *table, unsigned index,
                                const app_slave_presence_profile_t *profile,
                                bool *changed)
{
    if (changed)
        *changed = false;
    if (!table || index >= APP_SLAVE_PRESENCE_MAX_PEERS || !table->peers[index].used ||
        !app_slave_presence_profile_valid(profile))
        return false;
    app_slave_presence_peer_t *peer = &table->peers[index];
    bool same_source = strcmp(peer->profile.presence_uuid, profile->presence_uuid) == 0;
    if (same_source && strcmp(peer->profile.name, profile->name) == 0 &&
        peer->profile.timeout_seconds == profile->timeout_seconds &&
        peer->profile.blank_at_night == profile->blank_at_night)
        return true;
    /* Mezőnkénti másolás: a strukturális padding és a szöveg utáni bájtok nem
     * részei a profilnak, így nem indítanak felesleges tartós mentést. */
    app_slave_presence_profile_t clean = {0};
    memcpy(clean.name, profile->name, strlen(profile->name));
    memcpy(clean.presence_uuid, profile->presence_uuid, strlen(profile->presence_uuid));
    clean.timeout_seconds = profile->timeout_seconds;
    clean.blank_at_night = profile->blank_at_night;
    peer->profile = clean;
    peer->revision = next_revision(peer->revision);
    if (!same_source)
    {
        peer->counter = 0;
        peer->valid = false;
        peer->active = false;
    }
    if (changed)
        *changed = true;
    return true;
}

bool app_slave_presence_remove(app_slave_presence_table_t *table, unsigned index)
{
    if (!table || index >= APP_SLAVE_PRESENCE_MAX_PEERS || !table->peers[index].used)
        return false;
    memset(&table->peers[index], 0, sizeof(table->peers[index]));
    return true;
}

bool app_slave_presence_source(app_slave_presence_table_t *table, const char *uuid,
                               bool valid, double value)
{
    if (!table || !uuid || !uuid[0] || !uuid_valid(uuid))
        return false;
    bool matched = false;
    valid = valid && isfinite(value) && value >= 0;
    bool active = valid && value > 0;
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        app_slave_presence_peer_t *peer = &table->peers[i];
        if (!peer->used || strcmp(peer->profile.presence_uuid, uuid) != 0)
            continue;
        matched = true;
        if (active && (!peer->valid || !peer->active))
            ++peer->counter; /* A modulo-2^32 átfordulás a vezetéken is kezelt. */
        peer->valid = valid;
        peer->active = active;
    }
    return matched;
}

void app_slave_presence_invalidate(app_slave_presence_table_t *table)
{
    if (!table)
        return;
    for (unsigned i = 0; i < APP_SLAVE_PRESENCE_MAX_PEERS; ++i)
    {
        table->peers[i].valid = false;
        table->peers[i].active = false;
    }
}

static void write_u32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        out[i] = (uint8_t)(value >> (i * 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= (uint32_t)data[i] << (i * 8);
    return value;
}

bool app_slave_presence_encode(const app_slave_presence_peer_t *peer,
                               uint32_t sequence, bool live,
                               uint8_t out[APP_SLAVE_PRESENCE_PACKET_SIZE])
{
    if (!peer || !out || !peer->used || !peer->revision ||
        !app_slave_identity_valid(&peer->identity) ||
        !app_slave_presence_profile_valid(&peer->profile))
        return false;
    bool valid = live && peer->valid && peer->profile.presence_uuid[0];
    memset(out, 0, APP_SLAVE_PRESENCE_PACKET_SIZE);
    out[0] = 'Z';
    out[1] = 'P';
    out[2] = 1;
    out[3] = (peer->profile.blank_at_night ? 1 : 0) |
             (valid ? 2 : 0) | (valid && peer->active ? 4 : 0);
    write_u32(out + 4, peer->revision);
    write_u32(out + 8, peer->profile.timeout_seconds);
    write_u32(out + 12, peer->counter);
    write_u32(out + 16, sequence);
    return true;
}

static bool forward(uint32_t value, uint32_t previous)
{
    uint32_t difference = value - previous;
    return difference != 0 && difference < UINT32_C(0x80000000);
}

void app_slave_presence_disconnected(app_slave_presence_receiver_t *receiver)
{
    if (!receiver)
        return;
    receiver->baseline = false;
    receiver->valid = false;
    receiver->active = false;
    receiver->pending_pulse = false;
    receiver->pulse_received_us = 0;
    receiver->reset_presence = true;
}

void app_slave_presence_legacy(app_slave_presence_receiver_t *receiver)
{
    if (!receiver)
        return;
    memset(receiver, 0, sizeof(*receiver));
    receiver->reset_presence = true;
}

bool app_slave_presence_receive(app_slave_presence_receiver_t *receiver,
                                const uint8_t *data, size_t size,
                                bool authenticated, int64_t now_us)
{
    if (!receiver || !data || !authenticated || now_us < 0 ||
        size != APP_SLAVE_PRESENCE_PACKET_SIZE || data[0] != 'Z' ||
        data[1] != 'P' || data[2] != 1 || (data[3] & ~7u) ||
        ((data[3] & 4) && !(data[3] & 2)))
        return false;
    uint32_t revision = read_u32(data + 4);
    uint32_t timeout = read_u32(data + 8);
    uint32_t counter = read_u32(data + 12);
    uint32_t sequence = read_u32(data + 16);
    if (!revision || timeout < 1 || timeout > 86400 ||
        now_us < receiver->last_now_us ||
        (receiver->baseline && now_us < receiver->received_us))
        return false;
    bool new_revision = !receiver->baseline || revision != receiver->revision;
    if (receiver->baseline &&
        (!forward(sequence, receiver->sequence) ||
         (new_revision && !forward(revision, receiver->revision)) ||
         (!new_revision && counter != receiver->counter &&
          !forward(counter, receiver->counter))))
        return false;
    bool blank = (data[3] & 1) != 0;
    if (!new_revision && (blank != receiver->blank_at_night ||
                         timeout != receiver->timeout_seconds))
        return false;
    bool valid = (data[3] & 2) != 0;
    bool rebase = new_revision ||
                  now_us - receiver->received_us >= APP_SLAVE_PRESENCE_LEASE_US;
    bool pulse = valid && !rebase && forward(counter, receiver->counter);
    receiver->pending_pulse = valid &&
                              (pulse || (!rebase && receiver->pending_pulse));
    /* Egy új heartbeat nem fiatalíthatja meg a még át nem adott régi impulzust. */
    if (pulse)
        receiver->pulse_received_us = now_us;
    else if (!receiver->pending_pulse)
        receiver->pulse_received_us = 0;
    receiver->reset_presence |= rebase;
    receiver->baseline = true;
    receiver->have_profile = true;
    receiver->blank_at_night = blank;
    receiver->valid = valid;
    receiver->active = (data[3] & 4) != 0;
    receiver->revision = revision;
    receiver->sequence = sequence;
    receiver->counter = counter;
    receiver->timeout_seconds = timeout;
    receiver->received_us = now_us;
    return true;
}

void app_slave_presence_apply(app_slave_presence_receiver_t *receiver,
                              app_slave_display_t *display, int64_t now_us)
{
    if (!receiver || !display)
        return;
    /* Egy késve feldolgozott UI-üzenet nem forgathatja vissza a lease óráját. */
    int64_t now = now_us > receiver->last_now_us ? now_us : receiver->last_now_us;
    if (display->last_now_us > now)
        now = display->last_now_us;
    receiver->last_now_us = now;
    if (receiver->reset_presence)
    {
        app_slave_display_presence(display, false, false, now);
        receiver->reset_presence = false;
    }
    app_slave_display_profile_t profile = display->profile;
    profile.blank_at_night = receiver->have_profile && receiver->blank_at_night;
    if (receiver->have_profile)
        profile.presence_timeout_seconds = receiver->timeout_seconds;
    if (profile.blank_at_night != display->profile.blank_at_night ||
        profile.presence_timeout_seconds != display->profile.presence_timeout_seconds)
        (void)app_slave_display_profile(display, &profile, now);
    bool valid = receiver->baseline && receiver->valid && now >= receiver->received_us &&
                 now - receiver->received_us < APP_SLAVE_PRESENCE_LEASE_US;
    bool pulse_fresh = now >= receiver->pulse_received_us &&
                       now - receiver->pulse_received_us < APP_SLAVE_PRESENCE_LEASE_US;
    if (valid && receiver->pending_pulse && pulse_fresh)
        app_slave_display_presence(display, true, true, now);
    app_slave_display_presence(display, valid, valid && receiver->active, now);
    receiver->pending_pulse = false;
}
