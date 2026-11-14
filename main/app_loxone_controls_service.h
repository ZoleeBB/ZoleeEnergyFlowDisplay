#pragma once

#include "app_loxone_controls.h"
#include "esp_err.h"

/* A GUI csak az aktuális profilrevízióhoz tartozó, kis pillanatképet olvas.
 * A katalógus, a lista foglalása és a WebSocket a Loxone-workerben marad. */
bool app_loxone_controls_summary(const char *card_id, unsigned revision,
                                  lx_controls_summary_t *out);
bool app_loxone_controls_window(const char *card_id, unsigned revision, unsigned index,
                                 lx_controls_window_t *out);
/* Egy állapotverzióhoz tartozó vizuális indexsorrend. Eltérő verziónál
 * új snapshot kell; a mögöttes sorok és a szerver számlálói érintetlenek. */
bool app_loxone_controls_window_order(const char *card_id, unsigned revision,
                                       unsigned list_revision, uint16_t *out, unsigned capacity);
esp_err_t app_loxone_controls_command(const char *card_id, unsigned revision,
                                      lx_controls_action_t action);
esp_err_t app_loxone_controls_set(const char *card_id, unsigned revision, double value);
