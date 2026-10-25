#pragma once

#include "app_loxone_model.h"

/* Oldalanként fix kártyakeret, korlátos számú, elnevezhető Master-oldallal.
 * A konfiguráció külön objektum, nem növeli a hitelesítési struktúra másolatait. */
#define LX_HOME_MAX_CONSUMERS 12
#define LX_HOME_MAX_CARDS 12
#define LX_HOME_MAX_PAGES 8
#define LX_HOME_MAX_TOTAL_CARDS (LX_HOME_MAX_PAGES * LX_HOME_MAX_CARDS)
#define LX_HOME_JSON_MAX (128U * 1024U)
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
    /* Stabil vezérlőazonosító: a katalógus oldja fel a uuidAction parancscímet.
     * A source.uuid ettől független visszajelző állapot. */
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
    char id[LX_HOME_ID_SIZE];
    char title[LX_HOME_NAME_SIZE];
    unsigned first_card;
    unsigned card_count;
} lx_home_page_t;

#define LX_INTERCOM_GATE_ID "intercom-gate"
#define LX_INTERCOM_URL_SIZE 257

typedef struct
{
    bool enabled;
    uint8_t speaker_volume;
    uint8_t microphone_gain;
    bool ringtone_repeat;
    char ringtone[65];
    char stream_url[LX_INTERCOM_URL_SIZE];
    char video_username[96];
    /* Csak RAM-ban él; a publikus JSON és UI-pillanatkép nem tartalmazza. */
    char video_password[LX_PASSWORD_MAX];
    char bell_uuid[LX_UUID_LEN];
    /* Ugyanaz a pillanatnyi kártya és parancscél, mint az Otthon-oldalon. */
    lx_home_card_t gate;
} lx_intercom_config_t;

typedef struct
{
    lx_intercom_config_t intercom;
    unsigned consumer_count;
    unsigned card_count;
    unsigned page_count;
    lx_home_consumer_t consumers[LX_HOME_MAX_CONSUMERS];
    lx_home_card_t cards[LX_HOME_MAX_TOTAL_CARDS];
    lx_home_page_t pages[LX_HOME_MAX_PAGES];
} lx_home_config_t;

/* A régi, közvetlenül felépített egyoldalas C-profil is használható marad. */
static inline unsigned lx_home_page_count(const lx_home_config_t *config)
{
    if (!config || config->page_count > LX_HOME_MAX_PAGES ||
        config->card_count > LX_HOME_MAX_TOTAL_CARDS)
        return 0;
    return config->page_count ? config->page_count : config->card_count ? 1 : 0;
}

static inline bool lx_home_page_range(const lx_home_config_t *config, unsigned page,
                         unsigned *first, unsigned *count)
{
    if (!first || !count || page >= lx_home_page_count(config))
        return false;
    unsigned start = config->page_count ? config->pages[page].first_card : 0;
    unsigned length = config->page_count ? config->pages[page].card_count : config->card_count;
    if (length > LX_HOME_MAX_CARDS || start > config->card_count ||
        length > config->card_count - start)
        return false;
    *first = start;
    *count = length;
    return true;
}


/* Kizárólag konfiguráció, jelszó/token nélkül. Régi telepítésnél NULL = üres.
 * Hibánál az out változatlan; a hiba mezőt és elemsorszámot is tartalmaz. */
bool lx_home_config_parse(const cJSON *root, lx_home_config_t *out,
                          char error[LX_HOME_ERROR_SIZE]);
cJSON *lx_home_config_json(const lx_home_config_t *config);

/* Hiányzó átlagteljesítmény vagy tiltott javaslat sohasem ad indítási igényt. */
bool lx_home_consumer_start_kw(const lx_home_consumer_t *consumer, double *kw);

/* Korlátos JSON-beolvasás a hálózati/SD-s belépési pontoknak. A túl mély
 * dokumentumot még a rekurzív cJSON-parser előtt elutasítjuk. body[size] terminátor szükséges; hívó szabadítja. */
cJSON *lx_home_document_parse(const char *body, size_t size);

/* Üres/hiányzó jelszó csak azonos kamerához és felhasználóhoz tartható meg. */
bool lx_intercom_credentials_update(lx_intercom_config_t *next,
                                    const lx_intercom_config_t *saved,
                                    const cJSON *entry);
