#pragma once

#include "app_loxone_lighting.h"
#include "esp_err.h"

/* A GUI csak kérést és kis RAM-pillanatképet ad át. Sem hálózat,
 * sem JSON-feldolgozás, sem nagy foglalás nem történik a GUI feladatán. */
void app_loxone_home_visible(int page);
bool app_loxone_light_summary(const char *card_id, unsigned revision, lx_light_summary_t *out);

/* A nulla azonosító sikertelen megnyitást jelent. A nem nulla munkamenet
 * oldalváltás, profilcsere és kapcsolatvesztés után nem használható újra. */
unsigned app_loxone_light_open(const char *card_id, unsigned revision);
void app_loxone_light_close(unsigned handle);

typedef struct
{
    bool connected;
    bool ready;
    bool failed;
    unsigned circuit_count;
} app_loxone_light_info_t;

bool app_loxone_light_info(unsigned handle, app_loxone_light_info_t *out);
bool app_loxone_light_circuit(unsigned handle, unsigned index, lx_light_circuit_t *out);
esp_err_t app_loxone_light_set(unsigned handle, unsigned index, const lx_light_value_t *value);
