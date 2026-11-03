#pragma once

/*
 * A mentett eseménynapló megjelenítési sorrendje.
 *
 * A fájl függelékként épül - ez a helyes és olcsó írási mód, a fájl elejére
 * szúrás minden bejegyzésnél újraírná az egész napot -, a listát viszont a
 * legfrissebb sorral kezdve kényelmes olvasni, ahogy a kijelző diagnosztikai
 * nézete és az információs oldal is mutatja.
 *
 * A megfordítás helyben, második puffer nélkül történik: előbb a teljes szöveg
 * fordul meg, majd soronként vissza. Így a sorok sorrendje cserélődik, a soron
 * belüli szöveg viszont változatlan marad.
 */

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline void app_event_log_reverse_range(char *from, char *to)
{
    while (from < to) {
        char swap = *from;
        *from++ = *to;
        *to-- = swap;
    }
}

static inline void app_event_log_reverse_lines(char *text)
{
    size_t length = strlen(text);
    bool trailing = length && text[length - 1] == '\n';
    if (trailing) {
        text[--length] = 0;
    }
    if (!length) {
        /* Csak egy sortörés volt: azt visszaadjuk, hogy ne tűnjön el. */
        if (trailing) {
            text[0] = '\n';
            text[1] = 0;
        }
        return;
    }
    app_event_log_reverse_range(text, text + length - 1);
    char *line = text;
    for (char *at = text;; at++) {
        if (*at == '\n' || !*at) {
            app_event_log_reverse_range(line, at - 1);
            line = at + 1;
            if (!*at) {
                break;
            }
        }
    }
    if (trailing) {
        text[length] = '\n';
        text[length + 1] = 0;
    }
}
