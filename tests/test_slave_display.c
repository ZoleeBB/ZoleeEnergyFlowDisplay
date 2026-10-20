#include "app_slave_display.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define SECONDS(n) ((int64_t)(n) * 1000000)

int main(void)
{
    app_slave_display_profile_t profile = {83, 17, true, 30};
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

    /* Tartós aktív jel nem aludhat el; jelvesztés nem ad új 30 másodpercet. */
    app_slave_display_presence(&state, true, true, SECONDS(200));
    assert(app_slave_display_brightness(&state, SECONDS(240)) == 17);
    app_slave_display_presence(&state, false, false, SECONDS(241));
    assert(app_slave_display_brightness(&state, SECONDS(241)) == 0);
    app_slave_display_presence(&state, true, true, SECONDS(242));
    app_slave_display_presence(&state, true, false, SECONDS(250));
    assert(app_slave_display_brightness(&state, SECONDS(279)) == 17);
    assert(app_slave_display_brightness(&state, SECONDS(280)) == 0);

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

    /* Előző napi időzítő nem ébreszt; ténylegesen aktív jel igen. */
    profile = (app_slave_display_profile_t){77, 9, true, 30};
    assert(app_slave_display_init(&state, &profile, 0));
    app_slave_display_pointer(&state, true, SECONDS(1));
    app_slave_display_pointer(&state, false, SECONDS(2));
    app_slave_display_night(&state, true, SECONDS(3));
    assert(app_slave_display_brightness(&state, SECONDS(3)) == 0);
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
    printf("PASS: Slave day/night brightness, pulse/held presence, wake-only first touch, "
           "timeout, reconnect expiry, invalid profiles, monotonic/overflow; state=%zu bytes\n", sizeof(state));
}
