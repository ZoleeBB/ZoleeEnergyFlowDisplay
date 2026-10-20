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

    assert(!app_slave_presence_source(&table, "bad", true, 1));
    assert(!app_slave_presence_source(&table, "", true, 1));
    assert(app_slave_presence_source(&table, UUID_A, true, 0));
    assert(table.peers[0].valid && !table.peers[0].active && !table.peers[0].counter);
    assert(app_slave_presence_source(&table, UUID_A, true, 1));
    assert(table.peers[0].counter == 1 && table.peers[1].counter == 1);
    assert(!table.peers[2].valid && !table.peers[2].counter);
    assert(app_slave_presence_source(&table, UUID_A, true, 1));
    assert(table.peers[0].counter == 1);
    assert(app_slave_presence_source(&table, UUID_A, true, 0));
    assert(app_slave_presence_source(&table, UUID_A, true, 1));
    assert(table.peers[0].counter == 2);
    assert(app_slave_presence_source(&table, UUID_A, true, NAN));
    assert(!table.peers[0].valid && !table.peers[0].active);
    assert(app_slave_presence_source(&table, UUID_A, true, INFINITY));
    assert(!table.peers[0].valid);
    assert(app_slave_presence_source(&table, UUID_A, true, -1));
    assert(!table.peers[0].valid);
    assert(app_slave_presence_source(&table, UUID_A, true, 1));
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
    assert(app_slave_presence_source(&table, UUID_B, true, 1));
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

static void send_packet(app_slave_presence_receiver_t *receiver,
                         app_slave_presence_peer_t *peer, uint32_t sequence, int64_t second)
{
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    assert(app_slave_presence_encode(peer, sequence, true, wire));
    assert(app_slave_presence_receive(receiver, wire, sizeof(wire), true, SECONDS(second)));
}

static void wire_tests(void)
{
    app_slave_presence_peer_t peer = peer_fixture();
    peer.revision = UINT32_C(0x12345678);
    peer.counter = UINT32_C(0xabcdef12);
    peer.active = true;
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    uint8_t expected[] = {'Z', 'P', 1, 7, 0x78, 0x56, 0x34, 0x12,
                          30, 0, 0, 0, 0x12, 0xef, 0xcd, 0xab,
                          0x44, 0x33, 0x22, 0x11};
    assert(app_slave_presence_encode(&peer, UINT32_C(0x11223344), true, wire));
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
            wire[3] |= 8;
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
    assert(receiver.counter == peer.counter && receiver.revision == peer.revision);
    assert(app_slave_presence_encode(&peer, 1, false, wire) && wire[3] == 1);
    peer.profile.presence_uuid[0] = 0;
    assert(app_slave_presence_encode(&peer, 1, true, wire) && wire[3] == 1);
    peer.profile.timeout_seconds = 0;
    memset(wire, 0x42, sizeof(wire));
    assert(!app_slave_presence_encode(&peer, 1, true, wire));
    for (unsigned i = 0; i < sizeof(wire); ++i)
        assert(wire[i] == 0x42);
}

static void runtime_tests(void)
{
    app_slave_presence_peer_t peer = peer_fixture();
    app_slave_presence_receiver_t receiver = {0};
    app_slave_display_profile_t own = {83, 17, false, 30};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);
    peer.counter = 100; /* A kapcsolat előtti impulzusok nem játszhatók vissza. */
    send_packet(&receiver, &peer, 1, 1);
    app_slave_presence_apply(&receiver, &display, SECONDS(1));
    assert(display.profile.day_brightness == 83 && display.profile.night_brightness == 17);
    assert(app_slave_display_brightness(&display, SECONDS(1)) == 0);

    /* A termelő oldalon két BLE-poll közé eső rövid 1->0 is ébreszt. */
    peer.counter++;
    send_packet(&receiver, &peer, 2, 2);
    assert(receiver.pending_pulse && !receiver.active);
    app_slave_presence_apply(&receiver, &display, SECONDS(2));
    assert(app_slave_display_brightness(&display, SECONDS(2)) == 17);
    assert(!receiver.pending_pulse);
    send_packet(&receiver, &peer, 3, 3);
    app_slave_presence_apply(&receiver, &display, SECONDS(3));
    app_slave_presence_apply(&receiver, &display, SECONDS(31));
    assert(app_slave_display_brightness(&display, SECONDS(31)) == 17);
    app_slave_presence_apply(&receiver, &display, SECONDS(32));
    assert(app_slave_display_brightness(&display, SECONDS(32)) == 0);

    /* Profilváltás és hosszú csend utáni első minta is csak számlálóbázis. */
    peer.counter += 10;
    send_packet(&receiver, &peer, 4, 33);
    app_slave_presence_apply(&receiver, &display, SECONDS(33));
    assert(app_slave_display_brightness(&display, SECONDS(33)) == 0);
    peer.revision++;
    peer.counter++;
    peer.profile.timeout_seconds = 20;
    send_packet(&receiver, &peer, 5, 34);
    app_slave_presence_apply(&receiver, &display, SECONDS(34));
    assert(display.profile.presence_timeout_seconds == 20);
    assert(app_slave_display_brightness(&display, SECONDS(34)) == 0);

    peer.active = true;
    peer.counter++;
    send_packet(&receiver, &peer, 6, 35);
    app_slave_presence_apply(&receiver, &display, SECONDS(35));
    send_packet(&receiver, &peer, 7, 45);
    app_slave_presence_apply(&receiver, &display, SECONDS(45));
    assert(app_slave_display_brightness(&display, SECONDS(45)) == 17);
    peer.active = false;
    send_packet(&receiver, &peer, 8, 46);
    app_slave_presence_apply(&receiver, &display, SECONDS(46));
    int64_t awake = display.awake_until_us;
    send_packet(&receiver, &peer, 9, 47);
    app_slave_presence_apply(&receiver, &display, SECONDS(47));
    assert(display.awake_until_us == awake && awake == SECONDS(66));
    app_slave_presence_apply(&receiver, &display, SECONDS(66));
    assert(app_slave_display_brightness(&display, SECONDS(66)) == 0);

    peer.active = true;
    peer.counter++;
    send_packet(&receiver, &peer, 10, 67);
    app_slave_presence_apply(&receiver, &display, SECONDS(67));
    app_slave_presence_disconnected(&receiver);
    app_slave_presence_apply(&receiver, &display, SECONDS(68));
    assert(display.awake_until_us == SECONDS(87) && !display.presence_active);
    assert(display.profile.blank_at_night && display.night);
    peer.active = false;
    peer.counter += 100;
    send_packet(&receiver, &peer, 1, 100); /* Új kapcsolat, új sequence. */
    app_slave_presence_apply(&receiver, &display, SECONDS(100));
    assert(app_slave_display_brightness(&display, SECONDS(100)) == 0);
    peer.counter++;
    send_packet(&receiver, &peer, 2, 101);
    app_slave_presence_apply(&receiver, &display, SECONDS(101));
    assert(app_slave_display_brightness(&display, SECONDS(101)) == 17);

    /* Ténylegesen régi Master: a biztonságos alap nem sötétít. */
    app_slave_presence_legacy(&receiver);
    app_slave_presence_apply(&receiver, &display, SECONDS(200));
    assert(!display.profile.blank_at_night && display.profile.day_brightness == 83);
    assert(app_slave_display_brightness(&display, SECONDS(200)) == 17);
    app_slave_display_night(&display, false, SECONDS(201));
    assert(app_slave_display_brightness(&display, SECONDS(201)) == 83);
}

static void replay_and_lease_tests(void)
{
    app_slave_presence_peer_t peer = peer_fixture();
    app_slave_presence_receiver_t receiver = {0};
    app_slave_display_profile_t own = {80, 10, false, 1};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);
    peer.profile.timeout_seconds = 1;
    peer.counter = UINT32_MAX;
    send_packet(&receiver, &peer, UINT32_MAX, 1);
    app_slave_presence_apply(&receiver, &display, SECONDS(1));
    peer.counter = 0;
    send_packet(&receiver, &peer, 0, 2);
    app_slave_presence_apply(&receiver, &display, SECONDS(2));
    assert(app_slave_display_brightness(&display, SECONDS(2)) == 10);
    app_slave_presence_receiver_t saved = receiver;
    uint8_t wire[APP_SLAVE_PRESENCE_PACKET_SIZE];
    assert(app_slave_presence_encode(&peer, 0, true, wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);
    uint32_t invalid_sequences[] = {UINT32_MAX, UINT32_C(0x80000000)};
    for (unsigned i = 0; i < 2; ++i)
    {
        assert(app_slave_presence_encode(&peer, invalid_sequences[i], true, wire));
        assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
        assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);
    }
    peer.counter = UINT32_MAX; /* Regresszív számláló. */
    assert(app_slave_presence_encode(&peer, 1, true, wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    peer.counter = 0;
    peer.revision = UINT32_MAX; /* Regresszív profil. */
    assert(app_slave_presence_encode(&peer, 1, true, wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    peer.revision = 1;
    peer.profile.timeout_seconds = 2; /* Ugyanaz a revízió más profillal. */
    assert(app_slave_presence_encode(&peer, 1, true, wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(3)));
    assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);

    peer.profile.timeout_seconds = 1;
    peer.active = true;
    peer.counter++;
    send_packet(&receiver, &peer, 1, 3);
    app_slave_presence_apply(&receiver, &display, SECONDS(3));
    app_slave_presence_apply(&receiver, &display, SECONDS(17));
    assert(display.presence_active);
    int64_t awake = display.awake_until_us;
    app_slave_presence_apply(&receiver, &display, SECONDS(18));
    assert(!display.presence_active && display.awake_until_us == awake);
    assert(app_slave_display_brightness(&display, SECONDS(18)) == 0);
    peer.active = false;
    peer.counter++;
    send_packet(&receiver, &peer, 2, 19);
    app_slave_presence_apply(&receiver, &display, SECONDS(19));
    assert(app_slave_display_brightness(&display, SECONDS(19)) == 0);
    peer.counter++;
    send_packet(&receiver, &peer, 3, 20);
    app_slave_presence_apply(&receiver, &display, SECONDS(35)); /* Lejárt, még fel nem dolgozott impulzus. */
    assert(app_slave_display_brightness(&display, SECONDS(35)) == 0);
    assert(!receiver.pending_pulse);
    saved = receiver;
    assert(app_slave_presence_encode(&peer, 4, true, wire));
    assert(!app_slave_presence_receive(&receiver, wire, sizeof(wire), true, SECONDS(34)));
    assert(memcmp(&saved, &receiver, sizeof(receiver)) == 0);

    /* Érvénytelen érték nem tarthat ébren és nem ad érvényes lefutó élt. */
    peer.active = true;
    send_packet(&receiver, &peer, 4, 36);
    app_slave_presence_apply(&receiver, &display, SECONDS(36));
    awake = display.awake_until_us;
    peer.valid = false;
    send_packet(&receiver, &peer, 5, 37);
    app_slave_presence_apply(&receiver, &display, SECONDS(37));
    assert(!display.presence_active && display.awake_until_us == awake);
    assert(!receiver.valid && !receiver.active);

    /* Profilcserénél a régi forrás aktív szintjét nem szabad lefutó élnek venni. */
    peer.valid = true;
    peer.active = true;
    send_packet(&receiver, &peer, 6, 38);
    app_slave_presence_apply(&receiver, &display, SECONDS(38));
    awake = display.awake_until_us;
    peer.revision++;
    peer.counter = 0;
    peer.active = false;
    peer.profile.timeout_seconds = 100;
    send_packet(&receiver, &peer, 7, 39);
    app_slave_presence_apply(&receiver, &display, SECONDS(39));
    assert(display.awake_until_us == awake);
    assert(app_slave_display_brightness(&display, SECONDS(39)) == 0);

    /* A receiver saját monoton órája akkor is megmarad, ha a hívó visszalép. */
    app_slave_presence_apply(&receiver, &display, SECONDS(38));
    assert(receiver.last_now_us == SECONDS(39));
    app_slave_presence_disconnected(NULL);
    app_slave_presence_legacy(NULL);
    app_slave_presence_apply(NULL, &display, 0);
    app_slave_presence_apply(&receiver, NULL, 0);
}

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
    app_slave_display_profile_t own = {92, 13, false, 30};
    app_slave_display_t display;
    assert(app_slave_display_init(&display, &own, 0));
    app_slave_display_night(&display, true, 0);
    app_slave_presence_receiver_t receiver = {0};
    assert(app_slave_presence_source(&table, UUID_A, true, 0));
    send_packet(&receiver, &table.peers[0], 1, 1);
    app_slave_presence_apply(&receiver, &display, SECONDS(1));
    assert(app_slave_display_brightness(&display, SECONDS(1)) == 0);

    /* Ugyanazon BLE-poll előtt a valódi deltafeldolgozó már 1-et és 0-t is kapott. */
    assert(app_slave_presence_source(&table, UUID_A, true, 1));
    assert(app_slave_presence_source(&table, UUID_A, true, 0));
    assert(!table.peers[0].active && table.peers[0].counter == 1);
    send_packet(&receiver, &table.peers[0], 2, 2);
    send_packet(&receiver, &table.peers[0], 3, 3); /* Későbbi heartbeat ne nyelje el. */
    assert(receiver.pending_pulse);
    app_slave_presence_apply(&receiver, &display, SECONDS(3));
    assert(app_slave_display_brightness(&display, SECONDS(3)) == 13);
    assert(app_slave_display_brightness(&display, SECONDS(8)) == 0);
    assert(display.profile.day_brightness == 92 && display.profile.night_brightness == 13);

    assert(app_slave_presence_source(&table, UUID_A, true, 1));
    assert(app_slave_presence_source(&table, UUID_A, true, 0));
    send_packet(&receiver, &table.peers[0], 4, 9);
    assert(receiver.pending_pulse);
    app_slave_presence_invalidate(&table);
    send_packet(&receiver, &table.peers[0], 5, 10);
    assert(!receiver.pending_pulse);
    app_slave_presence_apply(&receiver, &display, SECONDS(10));
    assert(app_slave_display_brightness(&display, SECONDS(10)) == 0);
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
