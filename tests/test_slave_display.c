#include "app_slave_display.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SECONDS(n) ((int64_t)(n) * 1000000)

int main(void)
{
    app_slave_display_profile_t profile = {.day_brightness = 83, .night_brightness = 17,
        .blank_at_night = true, .presence_timeout_seconds = 30,
        .blank_on_presence_loss = false};
    app_slave_display_t state;
    assert(app_slave_display_init(&state, &profile, 0));
    assert(app_slave_display_brightness(&state, 0) == 83);
    app_slave_display_presence(&state, true, true, SECONDS(1));
    app_slave_display_presence(&state, true, false, SECONDS(2));
    assert(app_slave_display_brightness(&state, SECONDS(100)) == 83);
    app_slave_display_night(&state, true, SECONDS(101));
    assert(app_slave_display_brightness(&state, SECONDS(101)) == 0);

    /* Impulzus, újraindított timeout és az időhatár pontos viselkedése. */
    app_slave_display_presence(&state, true, true, SECONDS(102));
    assert(app_slave_display_brightness(&state, SECONDS(102)) == 17);
    app_slave_display_presence(&state, true, false, SECONDS(103));
    assert(app_slave_display_brightness(&state, SECONDS(132)) == 17);
    app_slave_display_presence(&state, true, true, SECONDS(132));
    app_slave_display_presence(&state, true, false, SECONDS(133));
    assert(app_slave_display_brightness(&state, SECONDS(162)) == 17);
    assert(app_slave_display_brightness(&state, SECONDS(163)) == 0);

    /* Az első teljes érintést lenyeljük; a második már rendesen kattinthat. */
    assert(app_slave_display_pointer(&state, true, SECONDS(164)));
    assert(app_slave_display_brightness(&state, SECONDS(164)) == 17);
    assert(app_slave_display_pointer(&state, true, SECONDS(165)));
    assert(app_slave_display_pointer(&state, false, SECONDS(166)));
    assert(!app_slave_display_pointer(&state, false, SECONDS(167)));
    assert(!app_slave_display_pointer(&state, true, SECONDS(168)));
    assert(!app_slave_display_pointer(&state, false, SECONDS(169)));
    assert(app_slave_display_brightness(&state, SECONDS(198)) == 17);
    assert(app_slave_display_brightness(&state, SECONDS(199)) == 0);

    /* Tartós aktív jel nem aludhat el, és a kimaradt minta sem jelenlétvesztés:
     * a tartás egy-két elvesztett BLE-olvasást áthidal. Utána a megszokott
     * visszaszámlálás indul – sötétíteni amúgy is csak a Master kérésére lehet,
     * ő látja a Loxone jelenlétállapotát. */
    app_slave_display_presence(&state, true, true, SECONDS(200));
    app_slave_display_presence(&state, false, false, SECONDS(201));
    for (int64_t second = 202; second <= 244; ++second)
        assert(app_slave_display_brightness(&state, SECONDS(second)) == 17);
    assert(app_slave_display_brightness(&state, SECONDS(245)) == 0);

    /* Friss, érvényes 0 azonnal elindítja a visszaszámlálást. */
    app_slave_display_presence(&state, true, true, SECONDS(300));
    app_slave_display_presence(&state, true, false, SECONDS(310));
    assert(app_slave_display_brightness(&state, SECONDS(339)) == 17);
    assert(app_slave_display_brightness(&state, SECONDS(340)) == 0);
    /* Egy friss aktív minta azonnal visszahozza a fényt. */
    app_slave_display_presence(&state, true, true, SECONDS(341));
    assert(app_slave_display_brightness(&state, SECONDS(341)) == 17);
    app_slave_display_presence(&state, true, false, SECONDS(342));
    assert(app_slave_display_brightness(&state, SECONDS(372)) == 0);

    /* Nappal saját nappali fényerő; az éjszakai sötétítés kikapcsolható. */
    app_slave_display_night(&state, false, SECONDS(281));
    assert(app_slave_display_brightness(&state, SECONDS(281)) == 83);
    profile.blank_at_night = false;
    assert(app_slave_display_profile(&state, &profile, SECONDS(282)));
    app_slave_display_night(&state, true, SECONDS(283));
    assert(app_slave_display_brightness(&state, SECONDS(300)) == 17);
    profile.day_brightness = 64;
    profile.night_brightness = 12;
    assert(app_slave_display_profile(&state, &profile, SECONDS(301)));
    assert(app_slave_display_brightness(&state, SECONDS(301)) == 12);
    app_slave_display_night(&state, false, SECONDS(302));
    assert(app_slave_display_brightness(&state, SECONDS(302)) == 64);

    app_slave_display_t saved = state;
    profile.night_brightness = 0;
    assert(!app_slave_display_profile(&state, &profile, SECONDS(303)));
    assert(!memcmp(&saved, &state, sizeof(state)));
    profile.night_brightness = 12;
    profile.presence_timeout_seconds = 0;
    assert(!app_slave_display_profile(&state, &profile, SECONDS(303)));
    profile.presence_timeout_seconds = 86401;
    assert(!app_slave_display_init(&state, &profile, 0));

    /* A napszakváltás nem törli a friss érintés hátralévő idejét. */
    profile = (app_slave_display_profile_t){.day_brightness = 77, .night_brightness = 9,
        .blank_at_night = true, .presence_timeout_seconds = 30,
        .blank_on_presence_loss = false};
    assert(app_slave_display_init(&state, &profile, 0));
    app_slave_display_pointer(&state, true, SECONDS(1));
    app_slave_display_pointer(&state, false, SECONDS(2));
    app_slave_display_night(&state, true, SECONDS(3));
    assert(app_slave_display_brightness(&state, SECONDS(3)) == 9);
    app_slave_display_presence(&state, true, true, SECONDS(4));
    app_slave_display_night(&state, false, SECONDS(5));
    app_slave_display_night(&state, true, SECONDS(6));
    assert(app_slave_display_brightness(&state, SECONDS(6)) == 9);

    /* Hosszú lenyomás, éjszaka közben érkező jelenlét és felszabadítás. */
    app_slave_display_presence(&state, true, false, SECONDS(7));
    assert(app_slave_display_brightness(&state, SECONDS(37)) == 0);
    assert(app_slave_display_pointer(&state, true, SECONDS(38)));
    assert(app_slave_display_brightness(&state, SECONDS(100)) == 9);
    app_slave_display_presence(&state, true, true, SECONDS(101));
    assert(app_slave_display_pointer(&state, false, SECONDS(102)));
    assert(!app_slave_display_pointer(&state, true, SECONDS(103)));
    app_slave_display_pointer(&state, false, SECONDS(104));

    /* A PIN/boot ideiglenes láthatósága nem generál jelenlétet vagy új timeoutot. */
    app_slave_display_t visible;
    assert(app_slave_display_init(&visible, &profile, 0));
    app_slave_display_night(&visible, true, SECONDS(1));
    app_slave_display_keep_visible(&visible, true, SECONDS(2));
    assert(app_slave_display_brightness(&visible, SECONDS(100)) == 9);
    assert(!visible.presence_active && !visible.awake_until_us);
    assert(!app_slave_display_pointer(&visible, true, SECONDS(101)));
    assert(!app_slave_display_pointer(&visible, false, SECONDS(102)));
    app_slave_display_keep_visible(&visible, false, SECONDS(103));
    assert(app_slave_display_brightness(&visible, SECONDS(131)) == 9);
    assert(app_slave_display_brightness(&visible, SECONDS(132)) == 0);
    app_slave_display_keep_visible(&visible, true, SECONDS(133));
    app_slave_display_keep_visible(&visible, false, SECONDS(134));
    assert(app_slave_display_brightness(&visible, SECONDS(134)) == 0);

    /* Hibás órasorrend és int64 túlcsordulás se okozhat azonnali elsötétülést. */
    app_slave_display_presence(&state, true, false, SECONDS(105));
    app_slave_display_presence(&state, true, true, SECONDS(106));
    app_slave_display_presence(&state, true, false, SECONDS(105));
    assert(app_slave_display_brightness(&state, SECONDS(135)) == 9);
    assert(app_slave_display_brightness(&state, SECONDS(136)) == 0);
    app_slave_display_pointer(&state, true, INT64_MAX - 100);
    app_slave_display_pointer(&state, false, INT64_MAX - 50);
    assert(app_slave_display_brightness(&state, INT64_MAX - 1) == 9);
    assert(app_slave_display_brightness(&state, INT64_MAX) == 0);
    /* Master jelenlétvezérlés: 1 -> 0 él azonnal sötétít, érintés időkorlátig ébreszt,
     * nappal a jelenlét nem sötétít, 1 érték azonnal az éjszakai fényerőre világosít. */
    app_slave_display_profile_t master = {.day_brightness = 70, .night_brightness = 25,
        .blank_at_night = true, .presence_timeout_seconds = 60,
        .blank_on_presence_loss = true};
    app_slave_display_t lobby;
    assert(app_slave_display_init(&lobby, &master, SECONDS(1)));
    app_slave_display_presence(&lobby, true, true, SECONDS(1));
    assert(app_slave_display_brightness(&lobby, SECONDS(2)) == 70);
    app_slave_display_presence(&lobby, true, false, SECONDS(3));
    assert(app_slave_display_brightness(&lobby, SECONDS(4)) == 70);
    app_slave_display_night(&lobby, true, SECONDS(5));
    assert(app_slave_display_brightness(&lobby, SECONDS(5)) == 0);
    app_slave_display_presence(&lobby, true, true, SECONDS(6));
    assert(app_slave_display_brightness(&lobby, SECONDS(6)) == 25);
    app_slave_display_presence(&lobby, true, false, SECONDS(7));
    assert(app_slave_display_brightness(&lobby, SECONDS(7)) == 0);
    assert(app_slave_display_pointer(&lobby, true, SECONDS(8)));
    assert(app_slave_display_brightness(&lobby, SECONDS(8)) == 25);
    assert(app_slave_display_pointer(&lobby, false, SECONDS(9)));
    assert(app_slave_display_brightness(&lobby, SECONDS(68)) == 25);
    assert(app_slave_display_brightness(&lobby, SECONDS(70)) == 0);
    /* Ismeretlen jelenlét (kapcsolatvesztés) nem sötétít a látható hibaképernyőn. */
    app_slave_display_keep_visible(&lobby, true, SECONDS(71));
    assert(app_slave_display_brightness(&lobby, SECONDS(72)) == 25);

    /* A két napszakkapcsoló mind a négy kombinációja, folyamatos jelenléttel
     * és pontosan a jelenlétvesztéstől mért időhatárral. */
    for (unsigned mask = 0; mask < 4; ++mask)
    {
        app_slave_display_profile_t daily = {
            .day_brightness = 83, .night_brightness = 17,
            .blank_at_day = (mask & 1) != 0, .blank_at_night = (mask & 2) != 0,
            .presence_timeout_seconds = 5,
        };
        app_slave_display_t display;
        assert(app_slave_display_init(&display, &daily, 0));
        for (int64_t second = 1; second <= 60; ++second)
        {
            app_slave_display_presence(&display, true, true, SECONDS(second));
            assert(app_slave_display_brightness(&display, SECONDS(second)) == 83);
        }
        app_slave_display_presence(&display, true, false, SECONDS(61));
        app_slave_display_presence(&display, true, false, SECONDS(65));
        assert(app_slave_display_brightness(&display, SECONDS(65)) == 83);
        assert(app_slave_display_brightness(&display, SECONDS(66)) == ((mask & 1) ? 0 : 83));
        app_slave_display_night(&display, true, SECONDS(67));
        assert(app_slave_display_brightness(&display, SECONDS(67)) == ((mask & 2) ? 0 : 17));
        app_slave_display_presence(&display, true, true, SECONDS(68));
        assert(app_slave_display_brightness(&display, SECONDS(68)) == 17);
    }

    /* Egy 1 másodperces profil későbbi aktív/inaktív mintája sem rövidítheti
     * le az érintés 15 másodpercét. A napszakváltás sem ad új időkeretet. */
    app_slave_display_profile_t short_profile = {
        .day_brightness = 80, .night_brightness = 10,
        .blank_at_day = true, .blank_at_night = true, .presence_timeout_seconds = 1,
    };
    app_slave_display_t touched;
    assert(app_slave_display_init(&touched, &short_profile, 0));
    assert(app_slave_display_pointer(&touched, true, SECONDS(1)));
    assert(app_slave_display_pointer(&touched, false, SECONDS(2)));
    app_slave_display_presence(&touched, true, true, SECONDS(3));
    app_slave_display_presence(&touched, true, false, SECONDS(4));
    app_slave_display_night(&touched, true, SECONDS(5));
    app_slave_display_presence(&touched, true, false, SECONDS(15));
    assert(app_slave_display_brightness(&touched, SECONDS(16)) == 10);
    assert(app_slave_display_brightness(&touched, SECONDS(17)) == 0);

    /* Közvetlen Master-forrás: az első érvényes 0 is késleltet, az invalid és
     * az ismételt 0 nem indítja újra. Új forrás nem örökli a régi jelenlétet. */
    short_profile.presence_timeout_seconds = 5;
    short_profile.delay_initial_absence = true;
    app_slave_display_t initial;
    assert(app_slave_display_init(&initial, &short_profile, 0));
    assert(app_slave_display_brightness(&initial, SECONDS(1)) == 80);
    app_slave_display_presence(&initial, false, false, SECONDS(2));
    app_slave_display_presence(&initial, true, false, SECONDS(3));
    app_slave_display_presence(&initial, true, false, SECONDS(7));
    assert(app_slave_display_brightness(&initial, SECONDS(7)) == 80);
    assert(app_slave_display_brightness(&initial, SECONDS(8)) == 0);
    assert(app_slave_display_pointer(&initial, true, SECONDS(9)));
    assert(app_slave_display_pointer(&initial, false, SECONDS(10)));
    app_slave_display_presence(&initial, true, true, SECONDS(11));
    app_slave_display_reset_presence(&initial, SECONDS(12));
    assert(!initial.presence_active && !initial.presence_known);
    assert(initial.awake_until_us == SECONDS(25));
    app_slave_display_presence(&initial, true, false, SECONDS(13));
    app_slave_display_night(&initial, true, SECONDS(14));
    assert(app_slave_display_brightness(&initial, SECONDS(24)) == 10);
    assert(app_slave_display_brightness(&initial, SECONDS(25)) == 0);
    app_slave_display_blanking(&initial, false, false, SECONDS(26));
    assert(app_slave_display_brightness(&initial, SECONDS(26)) == 10);
    app_slave_display_blanking(&initial, true, true, SECONDS(27));
    assert(app_slave_display_brightness(&initial, SECONDS(27)) == 0);
    /* Hosszú Loxone-kiesés nem hamis lefutó él: visszatérő 0-tól számolunk. */
    app_slave_display_presence(&initial, true, true, SECONDS(28));
    app_slave_display_presence(&initial, false, false, SECONDS(29));
    app_slave_display_keep_visible(&initial, true, SECONDS(29));
    assert(app_slave_display_brightness(&initial, SECONDS(90)) == 10);
    assert(initial.presence_active);
    app_slave_display_keep_visible(&initial, false, SECONDS(91));
    app_slave_display_presence(&initial, true, false, SECONDS(91));
    assert(app_slave_display_brightness(&initial, SECONDS(95)) == 10);
    assert(app_slave_display_brightness(&initial, SECONDS(96)) == 0);
    /* Aktív jelenlét közben rövidített profil után a lefutó él az új időt
     * követi; kizárólag a valódi érintés határideje élvez elsőbbséget. */
    short_profile.presence_timeout_seconds = 60;
    assert(app_slave_display_profile(&initial, &short_profile, SECONDS(100)));
    app_slave_display_presence(&initial, true, true, SECONDS(101));
    short_profile.presence_timeout_seconds = 5;
    assert(app_slave_display_profile(&initial, &short_profile, SECONDS(102)));
    app_slave_display_presence(&initial, true, false, SECONDS(103));
    assert(app_slave_display_brightness(&initial, SECONDS(107)) == 10);
    assert(app_slave_display_brightness(&initial, SECONDS(108)) == 0);
    /* Inaktív jelenlétnél mentett idő az eredeti 0 időpontjától számít.
     * Az új érintés már az új értéket használja, legalább 15 másodperccel. */
    short_profile.presence_timeout_seconds = 30;
    app_slave_display_t configured;
    assert(app_slave_display_init(&configured, &short_profile, 0));
    app_slave_display_presence(&configured, true, false, SECONDS(1));
    app_slave_display_presence(&configured, true, false, SECONDS(9));
    short_profile.presence_timeout_seconds = 5;
    assert(app_slave_display_profile(&configured, &short_profile, SECONDS(10)));
    assert(app_slave_display_brightness(&configured, SECONDS(10)) == 0);
    assert(app_slave_display_profile(&configured, &short_profile, SECONDS(11)));
    assert(app_slave_display_brightness(&configured, SECONDS(11)) == 0);
    short_profile.presence_timeout_seconds = 30;
    assert(app_slave_display_profile(&configured, &short_profile, SECONDS(12)));
    assert(configured.awake_until_us == SECONDS(31));
    app_slave_display_pointer(&configured, true, SECONDS(13));
    app_slave_display_pointer(&configured, false, SECONDS(14));
    assert(configured.touch_until_us == SECONDS(44));
    short_profile.presence_timeout_seconds = 5;
    assert(app_slave_display_profile(&configured, &short_profile, SECONDS(15)));
    assert(configured.awake_until_us == SECONDS(44));
    app_slave_display_pointer(&configured, true, SECONDS(16));
    app_slave_display_pointer(&configured, false, SECONDS(17));
    assert(configured.awake_until_us == SECONDS(32));
    app_slave_display_presence(&configured, true, false, SECONDS(25));
    assert(app_slave_display_brightness(&configured, SECONDS(31)) == 80);
    assert(app_slave_display_brightness(&configured, SECONDS(32)) == 0);
    /* A Slave első, már időzített 0-ja profilváltáskor sem ébredhet újra. */
    short_profile.delay_initial_absence = false;
    assert(app_slave_display_init(&configured, &short_profile, 0));
    app_slave_display_presence(&configured, true, false, SECONDS(1));
    short_profile.presence_timeout_seconds = 30;
    assert(app_slave_display_profile(&configured, &short_profile, SECONDS(2)));
    assert(app_slave_display_brightness(&configured, SECONDS(2)) == 0);
    printf("PASS: independent day/night flags, initial absence, held presence, touch minimum, source and day/night changes\n");
    printf("PASS: Master presence blanks immediately on 1->0, wakes on 1 and touch, day unaffected\n");
    printf("PASS: Slave day/night brightness, pulse/held presence, wake-only first touch, "
           "timeout, reconnect expiry, invalid profiles, monotonic/overflow; state=%zu bytes\n", sizeof(state));
}
