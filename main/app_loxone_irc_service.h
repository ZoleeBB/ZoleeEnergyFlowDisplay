#pragma once

#include "app_loxone_irc.h"

/* Kis, mutexelt pillanatkép a látható kártyához. A GUI nem dolgoz fel
 * katalógust vagy JSON-t, és nem indít külön hálózati kérést. */
bool app_loxone_irc_summary(const char *card_id, unsigned revision, lx_irc_summary_t *out);
