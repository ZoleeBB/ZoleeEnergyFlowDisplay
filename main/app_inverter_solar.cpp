#include "app_inverter_solar.h"
#include "third_party/SolarCalculator/SolarCalculator.h"
#include <cmath>
#include <ctime>

bool app_inverter_solar(int64_t utc, double lat, double lon, app_solar_position_t *position)
{
    if (!position || utc < 946684800 || !std::isfinite(lat) || !std::isfinite(lon) ||
        std::abs(lat) > 90 || std::abs(lon) > 180) return false;
    time_t epoch = utc;
    struct tm local, ut;
    if (!localtime_r(&epoch, &local) || !gmtime_r(&epoch, &ut) ||
        local.tm_year > 199) return false;
    calcHorizontalCoordinates(ut.tm_year + 1900, ut.tm_mon + 1, ut.tm_mday,
                              ut.tm_hour, ut.tm_min, ut.tm_sec, lat, lon,
                              position->azimuth, position->altitude);
    position->rise_minute = -1;
    position->set_minute = -1;
    position->night = position->altitude < -0.27;
    // A dátumválasztó vonal és az óraátállítás miatt az eseményt saját helyi idejére alakítjuk.
    time_t midnight = epoch - ut.tm_hour * 3600 - ut.tm_min * 60 - ut.tm_sec;
    for (int delta = -1; delta <= 1; ++delta) {
        time_t day = midnight + delta * 86400;
        double noon, rise, set;
        calcSunriseSunset(JulianDay(static_cast<unsigned long>(day)), lat, lon, noon, rise, set);
        double hours[] = {rise, set};
        int *minutes[] = {&position->rise_minute, &position->set_minute};
        for (unsigned i = 0; i < 2; ++i) {
            if (!std::isfinite(hours[i])) continue;
            time_t when = day + static_cast<time_t>(std::lround(hours[i] * 60)) * 60;
            struct tm event;
            if (localtime_r(&when, &event) && event.tm_year == local.tm_year && event.tm_yday == local.tm_yday)
                *minutes[i] = event.tm_hour * 60 + event.tm_min;
        }
    }
    int now = local.tm_hour * 60 + local.tm_min;
    int rise = position->rise_minute, set = position->set_minute;
    if (rise >= 0 && set >= 0)
        position->night = rise <= set ? now < rise || now >= set : now >= set && now < rise;
    return std::isfinite(position->altitude) && std::isfinite(position->azimuth);
}
