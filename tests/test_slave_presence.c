/* Valódi termelő/fogyasztó C-kód: két BLE-olvasás közti impulzus, célzás,
 * hibás wire-adat, replay, újracsatlakozás és a meglévő PWM-állapotgép együtt. */
#include "app_slave_presence.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SECONDS(n) (INT64_C(1000000) * (n))
static const char *UUID_A = "01234567-89ab-cdef-0123456789abcdef";
static const char *UUID_B = "abcdef01-2345-6789-abcdef0123456789";

static void identity_tests(void)
{
    app_slave_identity_t identity = {0, {1, 2, 3, 4, 5, 6}};
    char id[APP_SLAVE_PRESENCE_ID_SIZE];
    assert(app_slave_identity_id(&identity, id));
    assert(strcmp(id, "ble-0-060504030201") == 0);
    identity.type = 1;
    assert(!app_slave_identity_valid(&identity));
    identity.address[5] = 0x45; /* Resolvable private, nem identitás. */
    assert(!app_slave_identity_valid(&identity));
    identity.address[5] = 0xc5;
    assert(app_slave_identity_id(&identity, id));
    assert(strcmp(id, "ble-1-c50504030201") == 0);
    identity.type = 2;
    assert(!app_slave_identity_valid(&identity));
    identity = (app_slave_identity_t){0};
    assert(!app_slave_identity_valid(&identity));
    memset(identity.address, 0xff, sizeof(identity.address));
    assert(!app_slave_identity_valid(&identity));
    identity = (app_slave_identity_t){1, {0, 0, 0, 0, 0, 0xc0}};
    assert(!app_slave_identity_valid(&identity));
    identity.address[0] = 1;
    assert(app_slave_identity_valid(&identity));
    identity = (app_slave_identity_t){1, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    assert(!app_slave_identity_valid(&identity));
    assert(!app_slave_identity_valid(NULL));
    assert(!app_slave_identity_id(&identity, NULL));
}

static void profile_tests(void)
{
    app_slave_presence_profile_t profile = {
        .name = "Nappali – őrzött kijelző",
        .timeout_seconds = 1,
        .blank_at_night = true,
    };
    assert(app_slave_presence_profile_valid(&profile)); /* Üres UUID: érintésre ébreszt. */
    memcpy(profile.presence_uuid, UUID_A, 36);
    profile.timeout_seconds = 86400;
    assert(app_slave_presence_profile_valid(&profile));
    profile.timeout_seconds = 0;
    assert(!app_slave_presence_profile_valid(&profile));
    profile.timeout_seconds = 86401;
    assert(!app_slave_presence_profile_valid(&profile));
    profile.timeout_seconds = 30;
    const char *bad[] = {"", "   ", "\xc2\xa0", "\nNappali", "\xc0\xaf",
                         "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82",
                         "\x80", "\xc2\x85"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
    {
        memset(profile.name, 0, sizeof(profile.name));
        memcpy(profile.name, bad[i], strlen(bad[i]));
        assert(!app_slave_presence_profile_valid(&profile));
    }
    memset(profile.name, 'n', 80);
    profile.name[80] = 0;
    assert(app_slave_presence_profile_valid(&profile));
    profile.name[80] = 'x';
    assert(!app_slave_presence_profile_valid(&profile));
    profile.name[80] = 0;
    profile.presence_uuid[8] = '_';
    assert(!app_slave_presence_profile_valid(&profile));
    memcpy(profile.presence_uuid, UUID_A, 36);
    profile.presence_uuid[0] = 'A';
    assert(!app_slave_presence_profile_valid(&profile));
    memset(profile.presence_uuid, '0', sizeof(profile.presence_uuid));
    assert(!app_slave_presence_profile_valid(&profile));
}

static void table_tests(void)
{
    app_slave_presence_table_t table = {0};
    app_slave_identity_t identities[4] = {
        {0, {1, 2, 3, 4, 5, 6}}, {0, {2, 2, 3, 4, 5, 6}},
        {1, {1, 2, 3, 4, 5, 0xc6}}, {0, {4, 2, 3, 4, 5, 6}},
    };
    for (unsigned i = 0; i < 3; ++i)
        assert(app_slave_presence_register(&table, &identities[i], "Slave") == (int)i);
    assert(app_slave_presence_register(&table, &identities[3], "Negyedik") == -1);
    assert(app_slave_presence_register(&table, &identities[1], "Átnevezés tilos") == 1);
    assert(strcmp(table.peers[1].profile.name, "Slave") == 0);
    assert(app_slave_presence_find(&table, &identities[2]) == 2);
    assert(app_slave_presence_find(&table, &identities[3]) == -1);

    app_slave_presence_profile_t profile = table.peers[0].profile;
    memcpy(profile.presence_uuid, UUID_A, 36);
    profile.blank_at_night = true;
    bool changed = false;
    assert(app_slave_presence_update(&table, 0, &profile, &changed) && changed);
    assert(table.peers[0].revision == 2);
    changed = true;
    assert(app_slave_presence_update(&table, 0, &profile, &changed) && !changed);
    assert(table.peers[0].revision == 2);
    assert(app_slave_presence_update(&table, 1, &profile, &changed) && changed);
    memcpy(profile.presence_uuid, UUID_B, 36);
    assert(app_slave_presence_update(&table, 2, &profile, &changed) && changed);

    assert(!app_slave_presence_source(&table, "bad", true, 1, 0));
    assert(!app_slave_presence_source(&table, "", true, 1, 0));
    assert(app_slave_presence_source(&table, UUID_A, true, 0, 0));
    assert(table.peers[0].valid && !table.peers[0].active && !table.peers[0].counter);
    assert(app_slave_presence_source(&table, UUID_A, true, 1, 0));
    assert(table.peers[0].counter == 1 && table.peers[1].counter == 1);
    assert(!table.peers[2].valid && !table.peers[2].counter);
    assert(app_slave_presence_source(&table, UUID_A, true, 1, 0));
    assert(table.peers[0].counter == 1);
    assert(app_slave_presence_source(&table, UUID_A, true, 0, 0));
    assert(app_slave_presence_source(&table, UUID_A, true, 1, 0));
    assert(table.peers[0].counter == 2);
    assert(app_slave_presence_source(&table, UUID_A, true, NAN, 0));
    assert(!table.peers[0].valid && !table.peers[0].active);
    assert(app_slave_presence_source(&table, UUID_A, true, INFINITY, 0));
    assert(!table.peers[0].valid);
    assert(app_slave_presence_source(&table, UUID_A, true, -1, 0));
    assert(!table.peers[0].valid);
    assert(app_slave_presence_source(&table, UUID_A, true, 1, 0));
    assert(table.peers[0].counter == 3);
    app_slave_presence_invalidate(&table);
    assert(!table.peers[0].valid && !table.peers[1].active);
    assert(table.peers[0].counter == 3);

    /* Azonos szöveg eltérő paddinggel sem indít mentést. */
    profile = table.peers[0].profile;
    memset(profile.name + 6, 'x', sizeof(profile.name) - 6);
    assert(app_slave_presence_update(&table, 0, &profile, &changed) && !changed);
    profile.timeout_seconds = 0;
    app_slave_presence_table_t saved = table;
    assert(!app_slave_presence_update(&table, 0, &profile, &changed) && !changed);
    assert(memcmp(&table, &saved, sizeof(table)) == 0);
    assert(!app_slave_presence_update(&table, 3, &profile, &changed));

    profile = table.peers[0].profile;
    memcpy(profile.presence_uuid, UUID_B, 36);
    table.peers[0].revision = UINT32_MAX;
    assert(app_slave_presence_update(&table, 0, &profile, &changed) && changed);
    assert(table.peers[0].revision == 1 && !table.peers[0].counter && !table.peers[0].valid);
    table.peers[0].counter = UINT32_MAX;
    assert(app_slave_presence_source(&table, UUID_B, true, 1, 0));
    assert(table.peers[0].counter == 0);
    assert(app_slave_presence_remove(&table, 1));
    assert(!app_slave_presence_remove(&table, 1));
    assert(app_slave_presence_register(&table, &identities[3], "Új") == 1);
    assert(table.peers[1].revision == 1 && !table.peers[1].profile.blank_at_night);
}

static app_slave_presence_peer_t peer_fixture(void)
{
    app_slave_presence_peer_t peer = {
        .used = true,
        .identity = {0, {1, 2, 3, 4, 5, 6}},
        .profile = {.name = "Nappali", .timeout_seconds = 30, .blank_at_night = true},
        .revision = 1,
        .valid = true,
    };
    memcpy(peer.profile.presence_uuid, UUID_A, 36);
    return peer;
}

static void heartbeat_until(app_slave_presence_receiver_t *receiver,
                             app_slave_presence_peer_t *peer, app_slave_display_t *display,
                             int64_t from, int64_t to);

static void send_packet(app_slave_presence_receiver_t *receiver,
                         app_slave_presence_peer_t *peer, uint32_t sequence, int64_t second)
{
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    assert(app_slave_presence_encode(peer, sequence, true, false, SECONDS(second), wire));
    assert(app_slave_presence_receive(receiver, wire, sizeof(wire), true, SECONDS(second)));
}

/* A valódi Slave másodpercenként olvas: a tesztekben is friss marad a forrás,
 * különben a néma jelenlétforrás szándékosan nem engedi az elsötétítést. */
static void heartbeat_until(app_slave_presence_receiver_t *receiver,
                             app_slave_presence_peer_t *peer, app_slave_display_t *display,
                             int64_t from, int64_t to)
{
    for (int64_t second = from; second <= to; ++second)
    {
        send_packet(receiver, peer, (uint32_t)second, second);
        app_slave_presence_apply(receiver, display, SECONDS(second));
    }
}

static void wire_tests(void)
{
    app_slave_presence_peer_t peer = peer_fixture();
    peer.revision = UINT32_C(0x12345678);
    peer.counter = UINT32_C(0xabcdef12);
    peer.active = true;
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    /* Aktív jelenlét: érvényes és aktív jelzőbit, sötétítési kérés nélkül. */
    uint8_t expected[] = {'Z', 'P', 1, 6, 0x78, 0x56, 0x34, 0x12,
                          30, 0, 0, 0, 0x12, 0xef, 0xcd, 0xab,
                          0x44, 0x33, 0x22, 0x11};
    assert(app_slave_presence_encode(&peer, UINT32_C(0x11223344), true, false, 0, wire));
    assert(memcmp(wire, expected, sizeof(wire)) == 0);
    app_slave_presence_receiver_t receiver = {0};
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), false, 0));
    app_slave_presence_receiver_t saved = receiver;
    for (size_t size = 0; size < sizeof(wire); ++size)
        assert(!app_slave_presence_receive(&receiver, wire, size, true, 0));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire) + 1, true, 0));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, -1));
    for (unsigned i = 0; i < 9; ++i)
    {
        memcpy(wire, expected, sizeof(wire));
        if (i == 0)
            wire[0] = 'X';
        if (i == 1)
            wire[1] = 'X';
        if (i == 2)
            wire[2] = 2;
        if (i == 3)
            wire[3] |= 64; /* 3-4. bit: tájolás, 5.: OTA-kérés, a 6-7. tiltott. */
        if (i == 4)
            wire[3] = 4;
        if (i == 5)
            memset(wire + 4, 0, 4);
        if (i == 6)
            memset(wire + 8, 0, 4);
        if (i == 7)
            memset(wire + 8, 0xff, 4);
        if (i == 8)
        {
            wire[8] = 0x81;
            wire[9] = 0x51;
            wire[10] = 1; /* 86401. */
        }
        assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, 0));
        assert(memcmp(&receiver, &saved, sizeof(receiver)) == 0);
    }
    memcpy(wire, expected, sizeof(wire));
    assert(app_slave_presence_receive(&receiver, wire, sizeof(wire), true, 0));
    assert(receiver.active && receiver.valid && !receiver.pending_pulse);
    assert(!receiver.blank_at_night);
    assert(receiver.counter == peer.counter && receiver.revision == peer.revision);

    /* A 0. bit a Master pillanatnyi kérése. Beállított jelenlétforrásnál három
     * feltétel kell hozzá: élő forrás, megszűnt jelenlét, letelt időkorlát. */
    peer.active = false;
    assert(app_slave_presence_encode(&peer, 2, true, false, SECONDS(100), wire) && wire[3] == 2);
    peer.inactive_since_us = SECONDS(100);
    assert(app_slave_presence_encode(&peer, 3, true, false, SECONDS(129), wire) && wire[3] == 2);
    assert(app_slave_presence_encode(&peer, 4, true, false, SECONDS(130), wire) && wire[3] == 3);
    /* Néma (nem élő) Loxone-kapcsolat: a Slave inkább világos maradjon. */
    assert(app_slave_presence_encode(&peer, 5, false, false, SECONDS(130), wire) && wire[3] == 0);
    peer.valid = false;
    assert(app_slave_presence_encode(&peer, 6, true, false, SECONDS(130), wire) && wire[3] == 0);
    peer.valid = true;
    /* Jelenlétforrás nélkül csak az éjszakai beállítás és az érintés dönt. */
    peer.profile.presence_uuid[0] = 0;
    assert(app_slave_presence_encode(&peer, 7, true, false, 0, wire) && wire[3] == 1);
    peer.profile.blank_at_night = false;
    assert(app_slave_presence_encode(&peer, 8, true, false, 0, wire) && wire[3] == 0);
    peer.profile.blank_at_night = true;

    /* A sötétítési kérés ugyanazon a revízión belül is változhat: a Slave a
     * Master döntését követi. A tájolás és az időkorlát viszont profiladat. */
    app_slave_presence_receiver_t toggling = {0};
    memcpy(peer.profile.presence_uuid, UUID_A, 36);
    peer.revision = 7;
    peer.inactive_since_us = SECONDS(10);
    assert(app_slave_presence_encode(&peer, 10, true, false, SECONDS(50), wire));
    assert((wire[3] & 1) && app_slave_presence_receive(&toggling, wire, sizeof(wire), true, SECONDS(50)));
    assert(toggling.blank_at_night);
    peer.active = true;
    peer.counter++;
    assert(app_slave_presence_encode(&peer, 11, true, false, SECONDS(51), wire));
    assert(!(wire[3] & 1) && app_slave_presence_receive(&toggling, wire, sizeof(wire), true, SECONDS(51)));
    assert(!toggling.blank_at_night && toggling.active);
    peer.active = false;

    /* A tájolás a jelzőbájt 3-4. bitjén utazik, és a 0 fok bitjei nullák, így a
     * korábbi Slave is elfogadja az alapértelmezett csomagot. */
    for (uint16_t degrees = 0; degrees <= 270; degrees = (uint16_t)(degrees + 90))
    {
        peer.profile.rotation_degrees = degrees;
        peer.revision = UINT32_C(0x12345678) + degrees;
        assert(app_slave_presence_encode(&peer, UINT32_C(0x11223344) + degrees, true, false,
                                         SECONDS(50), wire));
        assert((wire[3] >> 3) == degrees / 90 && (wire[3] & 7) == 3);
        app_slave_presence_receiver_t rotated = {0};
        assert(app_slave_presence_receive(&rotated, wire, sizeof(wire), true, 0));
        assert(rotated.have_profile && rotated.rotation_degrees == degrees);
        /* Azonos revízióval nem változhat a profil: a visszafordított tájolás
         * ugyanúgy hamisítás, mint az időkorlát átírása. */
        uint8_t tampered[APP_SLAVE_PRESENCE_PACKET_SIZE];
        memcpy(tampered, wire, sizeof(tampered));
        tampered[3] = (uint8_t)((tampered[3] & 7) | (((degrees / 90 + 1) % 4) << 3));
        tampered[16]++;
        app_slave_presence_receiver_t saved_rotation = rotated;
        assert(!app_slave_presence_receive(&rotated, tampered, sizeof(tampered), true, 0));
        assert(memcmp(&rotated, &saved_rotation, sizeof(rotated)) == 0);
    }
    /* Az OTA-kérés szintjel: a felfutó él egyszer indít ellenőrzést, az azonos
     * szintű ismétlés és a kapcsolatvesztés utáni szint nem ad újabb kérést. */
    peer.profile.rotation_degrees = 0;
    peer.revision = UINT32_C(0x12345678);
    app_slave_presence_receiver_t ota = {0};
    assert(app_slave_presence_encode(&peer, 1, true, false, 0, wire));
    assert(!(wire[3] & 32) && app_slave_presence_receive(&ota, wire, sizeof(wire), true, 0));
    assert(!ota.ota_check_pending);
    assert(app_slave_presence_encode(&peer, 2, true, true, 0, wire));
    assert((wire[3] & 32) && app_slave_presence_receive(&ota, wire, sizeof(wire), true, 0));
    assert(ota.ota_check_pending && ota.ota_check_level);
    ota.ota_check_pending = false;
    assert(app_slave_presence_encode(&peer, 3, true, true, 0, wire));
    assert(app_slave_presence_receive(&ota, wire, sizeof(wire), true, 0));
    assert(!ota.ota_check_pending); /* Ugyanaz a szint nem indít új ellenőrzést. */
    assert(app_slave_presence_encode(&peer, 4, true, false, 0, wire));
    assert(app_slave_presence_receive(&ota, wire, sizeof(wire), true, 0));
    assert(!ota.ota_check_pending && !ota.ota_check_level);
    peer.profile.rotation_degrees = 45;
    assert(!app_slave_presence_profile_valid(&peer.profile));
    peer.profile.rotation_degrees = 360;
    assert(!app_slave_presence_profile_valid(&peer.profile));
    peer.profile.rotation_degrees = 0;
    peer.profile.presence_uuid[0] = 0;
    peer.profile.timeout_seconds = 0;
    memset(wire, 0x42, sizeof(wire));
    assert(!app_slave_presence_encode(&peer, 1, true, false, 0, wire));
    for (unsigned i = 0; i < sizeof(wire); ++i)
        assert(wire[i] == 0x42);
}

/* A teljes út: Loxone-delta -> Master peer -> 20 bájt -> Slave kijelző. A
 * sötétítés időzítése a Masteren van, mert csak ő látja a jelenlétállapotot. */
static void runtime_tests(void)
{
    app_slave_presence_table_t table = {0};
    app_slave_identity_t identity = {0, {1, 2, 3, 4, 5, 6}};
    assert(app_slave_presence_register(&table, &identity, "Háló") == 0);
    app_slave_presence_profile_t profile = table.peers[0].profile;
    profile.blank_at_night = true;
    profile.timeout_seconds = 30;
    memcpy(profile.presence_uuid, UUID_A, 36);
    assert(app_slave_presence_update(&table, 0, &profile, NULL));
    app_slave_presence_peer_t *peer = &table.peers[0];
    app_slave_presence_receiver_t receiver = {0};
    app_slave_display_profile_t own = {83, 17, false, 30, false};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);

    /* A beállított forrás még nem szólalt meg: a kijelző szándékosan világos. */
    send_packet(&receiver, peer, 1, 1);
    app_slave_presence_apply(&receiver, &display, SECONDS(1));
    assert(!receiver.valid && !receiver.blank_at_night && !display.profile.blank_at_night);
    assert(display.profile.day_brightness == 83 && display.profile.night_brightness == 17);
    assert(app_slave_display_brightness(&display, SECONDS(1)) == 17);

    /* Folyamatos 1-es jelenlétjel: a kijelző soha nem sötétedik el. */
    assert(app_slave_presence_source(&table, UUID_A, true, 1, SECONDS(2)));
    heartbeat_until(&receiver, peer, &display, 2, 60);
    assert(receiver.valid && receiver.active && !receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(60)) == 17);
    /* Kimaradó BLE-olvasások alatt sem: a Master utolsó szava a világos kijelző. */
    assert(app_slave_display_brightness(&display, SECONDS(600)) == 17);

    /* A jelenlét megszűnése indítja a visszaszámlálást, nem az éjszaka kezdete. */
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(601)));
    heartbeat_until(&receiver, peer, &display, 601, 630);
    assert(!receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(630)) == 17);
    heartbeat_until(&receiver, peer, &display, 631, 631);
    assert(receiver.blank_at_night && display.profile.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(631)) == 0);

    /* Érintés a vaksötétben: az első érintést lenyeljük, de ébreszt. */
    assert(app_slave_display_pointer(&display, true, SECONDS(632)));
    assert(app_slave_display_brightness(&display, SECONDS(632)) == 17);
    assert(app_slave_display_pointer(&display, false, SECONDS(633)));
    /* A felengedés is ad teljes időkorlátot: 633 + 30 másodperc. */
    assert(app_slave_display_brightness(&display, SECONDS(662)) == 17);
    assert(app_slave_display_brightness(&display, SECONDS(663)) == 0);

    /* Visszatérő jelenlét azonnal visszavonja a Master kérését. */
    assert(app_slave_presence_source(&table, UUID_A, true, 1, SECONDS(700)));
    send_packet(&receiver, peer, 700, 700);
    app_slave_presence_apply(&receiver, &display, SECONDS(700));
    assert(!display.profile.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(700)) == 17);

    /* Loxone-kiesés: a forrás érvénytelen lesz, a kijelző világos marad. */
    app_slave_presence_invalidate(&table);
    send_packet(&receiver, peer, 701, 701);
    app_slave_presence_apply(&receiver, &display, SECONDS(701));
    assert(!receiver.valid && !display.profile.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(1000)) == 17);

    /* BLE-kapcsolatvesztés: az utolsó döntés marad érvényben. */
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(1001)));
    heartbeat_until(&receiver, peer, &display, 1001, 1031);
    assert(app_slave_display_brightness(&display, SECONDS(1031)) == 0);
    app_slave_presence_disconnected(&receiver);
    app_slave_presence_apply(&receiver, &display, SECONDS(1032));
    assert(display.profile.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(1200)) == 0);

    /* Ténylegesen régi Master: a biztonságos alap nem sötétít. */
    app_slave_presence_legacy(&receiver);
    app_slave_presence_apply(&receiver, &display, SECONDS(1201));
    assert(!display.profile.blank_at_night && display.profile.day_brightness == 83);
    assert(app_slave_display_brightness(&display, SECONDS(1201)) == 17);
    app_slave_display_night(&display, false, SECONDS(1202));
    assert(app_slave_display_brightness(&display, SECONDS(1202)) == 83);
}

static void replay_and_lease_tests(void)
{
    app_slave_presence_peer_t peer = peer_fixture();
    app_slave_presence_receiver_t receiver = {0};
    app_slave_display_profile_t own = {80, 10, false, 1, false};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);
    peer.profile.timeout_seconds = 1;
    peer.counter = UINT32_MAX;
    send_packet(&receiver, &peer, UINT32_MAX, 1);
    app_slave_presence_apply(&receiver, &display, SECONDS(1));
    peer.counter = 0; /* Átfordulás: a számláló előre lép. */
    send_packet(&receiver, &peer, 0, 2);
    app_slave_presence_apply(&receiver, &display, SECONDS(2));
    assert(app_slave_display_brightness(&display, SECONDS(2)) == 10);
    app_slave_presence_receiver_t saved = receiver;
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    assert(app_slave_presence_encode(&peer, 0, true, false, SECONDS(3), wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);
    uint32_t invalid_sequences[] = {UINT32_MAX, UINT32_C(0x80000000)};
    for (unsigned i = 0; i < 2; ++i)
    {
        assert(app_slave_presence_encode(&peer, invalid_sequences[i], true, false, SECONDS(3), wire));
        assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
        assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);
    }
    peer.counter = UINT32_MAX; /* Regresszív számláló. */
    assert(app_slave_presence_encode(&peer, 1, true, false, SECONDS(3), wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    peer.counter = 0;
    peer.revision = UINT32_MAX; /* Regresszív profil. */
    assert(app_slave_presence_encode(&peer, 1, true, false, SECONDS(3), wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    peer.revision = 1;
    peer.profile.timeout_seconds = 2; /* Ugyanaz a revízió más profillal. */
    assert(app_slave_presence_encode(&peer, 1, true, false, SECONDS(3), wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);
    peer.profile.timeout_seconds = 1;

    /* Aktív jelenlét: a lejárt lease és a néma Master sem sötétít el. Pontosan
     * ez volt a hibajelenség: folyamatos jelenlétjel mellett is elaludt. */
    peer.active = true;
    peer.counter++;
    send_packet(&receiver, &peer, 1, 3);
    app_slave_presence_apply(&receiver, &display, SECONDS(3));
    assert(display.presence_active);
    app_slave_presence_apply(&receiver, &display, SECONDS(18));
    assert(!display.profile.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(300)) == 10);

    /* A Master a jelenlét megszűnése után az időkorláttal kéri a sötétítést. */
    peer.active = false;
    peer.inactive_since_us = SECONDS(301);
    peer.counter++;
    send_packet(&receiver, &peer, 2, 301);
    app_slave_presence_apply(&receiver, &display, SECONDS(301));
    assert(!receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(301)) == 10);
    send_packet(&receiver, &peer, 3, 302);
    app_slave_presence_apply(&receiver, &display, SECONDS(302));
    assert(receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(302)) == 0);

    /* Érintés 1 másodperces időkorlát mellett is olvasható ideig világít. */
    assert(app_slave_display_pointer(&display, true, SECONDS(303)));
    assert(app_slave_display_pointer(&display, false, SECONDS(303)));
    assert(app_slave_display_brightness(&display, SECONDS(317)) == 10);
    assert(app_slave_display_brightness(&display, SECONDS(318)) == 0);

    /* Érvénytelen érték nem ad érvényes lefutó élt és nem ad új időkorlátot. */
    peer.active = true;
    peer.inactive_since_us = 0;
    peer.counter++;
    send_packet(&receiver, &peer, 4, 320);
    app_slave_presence_apply(&receiver, &display, SECONDS(320));
    int64_t awake = display.awake_until_us;
    peer.valid = false;
    send_packet(&receiver, &peer, 5, 321);
    app_slave_presence_apply(&receiver, &display, SECONDS(321));
    assert(display.presence_active && display.awake_until_us == awake);
    assert(!receiver.valid && !receiver.active && !receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(321)) == 10);

    /* A receiver saját monoton órája akkor is megmarad, ha a hívó visszalép. */
    app_slave_presence_apply(&receiver, &display, SECONDS(300));
    assert(receiver.last_now_us == SECONDS(321));
    app_slave_presence_disconnected(NULL);
    app_slave_presence_legacy(NULL);
    app_slave_presence_apply(NULL, &display, 0);
    app_slave_presence_apply(&receiver, NULL, 0);
}

/* A két BLE-olvasás közé eső rövid 1->0 jelenlétimpulzus nem veszhet el. */
static void actual_delta_to_display_test(void)
{
    app_slave_presence_table_t table = {0};
    app_slave_identity_t identity = {0, {1, 2, 3, 4, 5, 6}};
    assert(app_slave_presence_register(&table, &identity, "Háló") == 0);
    app_slave_presence_profile_t profile = table.peers[0].profile;
    profile.blank_at_night = true;
    profile.timeout_seconds = 5;
    memcpy(profile.presence_uuid, UUID_A, 36);
    assert(app_slave_presence_update(&table, 0, &profile, NULL));
    app_slave_display_profile_t own = {92, 13, false, 30, false};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);
    app_slave_presence_receiver_t receiver = {0};
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(1)));
    send_packet(&receiver, &table.peers[0], 1, 1);
    app_slave_presence_apply(&receiver, &display, SECONDS(1));
    assert(app_slave_display_brightness(&display, SECONDS(1)) == 13);
    send_packet(&receiver, &table.peers[0], 6, 6);
    app_slave_presence_apply(&receiver, &display, SECONDS(6));
    assert(app_slave_display_brightness(&display, SECONDS(6)) == 0);

    /* Ugyanazon BLE-poll előtt a valódi deltafeldolgozó már 1-et és 0-t is kapott. */
    assert(app_slave_presence_source(&table, UUID_A, true, 1, SECONDS(7)));
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(7)));
    assert(!table.peers[0].active && table.peers[0].counter == 1);
    send_packet(&receiver, &table.peers[0], 7, 7);
    send_packet(&receiver, &table.peers[0], 8, 8); /* Későbbi heartbeat ne nyelje el. */
    assert(receiver.pending_pulse);
    app_slave_presence_apply(&receiver, &display, SECONDS(8));
    assert(app_slave_display_brightness(&display, SECONDS(8)) == 13);
    /* A helyi impulzusébresztés az időkorláttal jár le, és közben a Master is
     * megkéri a sötétítést (jelenlétvesztés + 5 másodperc). */
    heartbeat_until(&receiver, &table.peers[0], &display, 9, 12);
    assert(receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(13)) == 0);
    assert(display.profile.day_brightness == 92 && display.profile.night_brightness == 13);

    assert(app_slave_presence_source(&table, UUID_A, true, 1, SECONDS(14)));
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(14)));
    send_packet(&receiver, &table.peers[0], 13, 14);
    assert(receiver.pending_pulse);
    app_slave_presence_invalidate(&table);
    send_packet(&receiver, &table.peers[0], 14, 15);
    assert(!receiver.pending_pulse);
    app_slave_presence_apply(&receiver, &display, SECONDS(15));
    /* Érvénytelenné vált forrás (Loxone-kiesés) nem sötétít: világos marad. */
    assert(!display.profile.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(15)) == 13);

    /* Folyamatos inaktív heartbeat sem fiatalíthatja a UI-ra váró régi impulzust. */
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(16)));
    send_packet(&receiver, &table.peers[0], 15, 16);
    assert(app_slave_presence_source(&table, UUID_A, true, 1, SECONDS(17)));
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(17)));
    send_packet(&receiver, &table.peers[0], 16, 17);
    assert(receiver.pending_pulse && receiver.pulse_received_us == SECONDS(17));
    send_packet(&receiver, &table.peers[0], 17, 25);
    assert(receiver.pending_pulse && receiver.pulse_received_us == SECONDS(17));
    /* A lease-nél régebbi impulzus már nem ébreszt, a Master pedig sötétít. */
    app_slave_presence_apply(&receiver, &display, SECONDS(32));
    assert(receiver.blank_at_night);
    assert(app_slave_display_brightness(&display, SECONDS(32)) == 0);

    /* Újabb valódi impulzus viszont frissíti az esemény saját idejét. */
    assert(app_slave_presence_source(&table, UUID_A, true, 1, SECONDS(33)));
    assert(app_slave_presence_source(&table, UUID_A, true, 0, SECONDS(33)));
    send_packet(&receiver, &table.peers[0], 18, 33);
    send_packet(&receiver, &table.peers[0], 19, 34);
    app_slave_presence_apply(&receiver, &display, SECONDS(34));
    assert(app_slave_display_brightness(&display, SECONDS(34)) == 13);
}

int main(void)
{
    identity_tests();
    profile_tests();
    table_tests();
    wire_tests();
    runtime_tests();
    replay_and_lease_tests();
    actual_delta_to_display_test();
    printf("PASS: three bonded identities, strict profiles, 20-byte wire, pulse capture, "
           "replay/wrap/reconnect/lease, local brightness; table=%zu receiver=%zu bytes\n",
           sizeof(app_slave_presence_table_t), sizeof(app_slave_presence_receiver_t));
    return 0;
}
