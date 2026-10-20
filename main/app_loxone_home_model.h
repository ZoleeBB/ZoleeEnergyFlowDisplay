#pragma once

#include "app_loxone_model.h"

/* A két Master-oldal fix felső korlátja; az elrendezés egy képernyős marad.
 * A konfiguráció külön objektum, nem növeli a hitelesítési struktúra másolatait. */
#define LX_HOME_MAX_CONSUMERS 12
#define LX_HOME_MAX_CARDS 12
#define LX_HOME_NAME_SIZE 81
#define LX_HOME_ID_SIZE 33
#define LX_HOME_UNIT_SIZE 17
#define LX_HOME_ERROR_SIZE 192

typedef struct
{
    char id[LX_HOME_ID_SIZE];
    char name[LX_HOME_NAME_SIZE];
    char icon[LX_HOME_ID_SIZE];
    lx_mapping_t source;
    double average_kw;
    bool average_known;
    bool suggest_start;
} lx_home_consumer_t;

typedef enum
{
    LX_HOME_CARD_VALUE,
    LX_HOME_CARD_PULSE,
    LX_HOME_CARD_SWITCH
} lx_home_card_type_t;

typedef struct
{
    char id[LX_HOME_ID_SIZE];
    char name[LX_HOME_NAME_SIZE];
    char icon[LX_HOME_ID_SIZE];
    lx_home_card_type_t type;
    lx_mapping_t source;
    /* A parancs a control UUID-ra megy; a source.uuid visszajelző állapot. */
    char control_uuid[LX_UUID_LEN];
    char unit[LX_HOME_UNIT_SIZE];
    unsigned decimals;
    bool warning_low_set;
    bool warning_high_set;
    double warning_low;
    double warning_high;
    char warning_color[8];
} lx_home_card_t;

typedef struct
{
    unsigned consumer_count;
    unsigned card_count;
    lx_home_consumer_t consumers[LX_HOME_MAX_CONSUMERS];
    lx_home_card_t cards[LX_HOME_MAX_CARDS];
} lx_home_config_t;

/* Kizárólag konfiguráció, jelszó/token nélkül. Régi telepítésnél NULL = üres.
 * Hibánál az out változatlan; a hiba mezőt és elemsorszámot is tartalmaz. */
bool lx_home_config_parse(const cJSON *root, lx_home_config_t *out,
                          char error[LX_HOME_ERROR_SIZE]);
cJSON *lx_home_config_json(const lx_home_config_t *config);

/* Hiányzó átlagteljesítmény vagy tiltott javaslat sohasem ad indítási igényt. */
bool lx_home_consumer_start_kw(const lx_home_consumer_t *consumer, double *kw);
