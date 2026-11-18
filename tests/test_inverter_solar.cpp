#include "app_inverter_solar.h"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <cmath>

int main() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    app_solar_position_t sun;
    // Budapest, június 21., valódi helyi idő és nyári időszámítás.
    struct tm date = {};
    date.tm_year = 126; date.tm_mon = 5; date.tm_mday = 21; date.tm_hour = 12; date.tm_isdst = -1;
    time_t noon = mktime(&date);
    assert(app_inverter_solar(noon, 47.498, 19.04, &sun) && !sun.night);
    assert(sun.rise_minute >= 282 && sun.rise_minute <= 290);
    assert(sun.set_minute >= 1240 && sun.set_minute <= 1248);
    assert(sun.altitude > 60 && sun.azimuth > 140 && sun.azimuth < 190);
    int set = sun.set_minute, rise = sun.rise_minute;
    time_t midnight = noon - 12 * 3600;
    assert(app_inverter_solar(midnight + set * 60, 47.498, 19.04, &sun) && sun.night);
    assert(app_inverter_solar(midnight + rise * 60, 47.498, 19.04, &sun) && !sun.night);
    assert(app_inverter_solar(noon, 80, 0, &sun) && !sun.night && sun.rise_minute == -1);
    assert(!app_inverter_solar(noon, 91, 0, &sun));
}
