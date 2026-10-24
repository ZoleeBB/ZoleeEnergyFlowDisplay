#pragma once

#include "app_loxone_home_model.h"

/* A Loxone állapotfolyam delta alapú: az első snapshotból megőrzött 24 szám
 * nem képi/widget-adat, és nem hoz létre további Miniserver-kapcsolatot. */
typedef struct
{
    double value;
    bool valid;
} lx_home_value_t;

typedef struct
{
    lx_home_value_t consumers[LX_HOME_MAX_CONSUMERS];
    lx_home_value_t cards[LX_HOME_MAX_TOTAL_CARDS];
    bool command_supported[LX_HOME_MAX_TOTAL_CARDS];
} lx_home_values_t;

typedef enum
{
    LX_HOME_COMMAND_PULSE,
    LX_HOME_COMMAND_ON,
    LX_HOME_COMMAND_OFF
} lx_home_command_t;

/* Teljes numerikus WS-tábla; formahibánál a korábbi állapot változatlan.
 * Nem véges/negatív fogyasztás és nem bináris kapcsoló csak az érintett
 * értéket teszi érvénytelenné, nem hamis nulla mérést ad. */
bool lx_home_values_update(lx_home_values_t *values, const lx_home_config_t *config,
                           int type, const uint8_t *data, size_t size);
/* Csak a konfigurált kártya és a jelenlegi, nem védett, megfelelő típusú
 * struktúraobjektum adhat parancsot. Tetszőleges URL/parancsszöveg nem bemenet. */
bool lx_home_command_build(const lx_home_config_t *config, const cJSON *catalog,
                           const char *card_id, lx_home_command_t action,
                           char out[96]);
bool lx_home_control_supported(const char *type, bool secured, bool pulse);

/* Csak egy kifejezett 0/1 módosítja a csengetés reteszét. Hiányzó állapot,
 * más delta, NaN és kapcsolatvesztés nem jelent csengetésvéget. */
bool lx_intercom_bell_update(bool *ringing, const lx_intercom_config_t *config,
                             int type, const uint8_t *data, size_t size);
