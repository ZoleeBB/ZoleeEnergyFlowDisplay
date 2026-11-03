/* A mentett napló megjelenítési sorrendje: a legfrissebb sor kerül előre. */
#include "app_event_log_view.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char three[256] = "elso\nmasodik\nharmadik\n";
    app_event_log_reverse_lines(three);
    assert(!strcmp(three, "harmadik\nmasodik\nelso\n"));

    /* Lezáró sortörés nélkül is helyes (a 64 KiB-os vég beolvasásánál előfordul). */
    char partial[256] = "elso\nmasodik";
    app_event_log_reverse_lines(partial);
    assert(!strcmp(partial, "masodik\nelso"));

    /* Határesetek: egy sor, üres szöveg, csak sortörés, üres sor a közepén. */
    char single[256] = "csak egy sor\n";
    app_event_log_reverse_lines(single);
    assert(!strcmp(single, "csak egy sor\n"));
    char empty[256] = "";
    app_event_log_reverse_lines(empty);
    assert(!strcmp(empty, ""));
    char newline[256] = "\n";
    app_event_log_reverse_lines(newline);
    assert(!strcmp(newline, "\n"));
    char hole[256] = "a\n\nb\n";
    app_event_log_reverse_lines(hole);
    assert(!strcmp(hole, "b\n\na\n"));

    /* Valódi naplósorok: a soron belüli szöveg változatlan marad. */
    char log[256] = "2026-11-03 10:00:00 INFO     Rendszerindítás befejeződött.\n"
                    "2026-11-03 10:00:05 FIGYELEM Wi-Fi kapcsolat megszakadt.\n";
    app_event_log_reverse_lines(log);
    assert(!strncmp(log, "2026-11-03 10:00:05 FIGYELEM Wi-Fi kapcsolat megszakadt.\n", 56));
    assert(strstr(log, "2026-11-03 10:00:00 INFO     Rendszerindítás befejeződött.\n"));

    puts("PASS: stored log is shown newest first, lines unchanged, partial and empty input safe");
    return 0;
}
