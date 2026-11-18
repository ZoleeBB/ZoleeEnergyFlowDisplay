#pragma once

#include <stdbool.h>
#include <ctype.h>
#include <string.h>

/* A hívó biztosítja a legalább 10 bájtos ÉÉÉÉ-HH-NN mezőt. A BLE
 * kompatibilitási ablaka szűkebb, mint a kártyán lévő napfájloké. */
static inline bool app_stats_date_prefix_valid(const char *s, unsigned min_year, unsigned max_year)
{
    if (s[4] != '-' || s[7] != '-') return false;
    for (unsigned i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
    unsigned year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    unsigned month = (s[5] - '0') * 10 + (s[6] - '0');
    unsigned day = (s[8] - '0') * 10 + (s[9] - '0');
    static const unsigned char days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < min_year || year > max_year || month < 1 || month > 12) return false;
    unsigned maximum = days[month - 1] +
        (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    return day > 0 && day <= maximum;
}

static inline bool app_stats_history_file(const char *name)
{
    size_t length = strlen(name);
    if ((length != 15 && length != 19) || !app_stats_date_prefix_valid(name, 1, 9999))
        return false;
    char suffix[10]; /* FAT megőrizheti a kiterjesztések nagybetűs alakját. */
    for (size_t i = 10; i <= length; ++i) suffix[i - 10] = (char)tolower((unsigned char)name[i]);
    return !strcmp(suffix, ".json") || !strcmp(suffix, ".json.tmp") || !strcmp(suffix, ".json.bak");
}
