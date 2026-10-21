#pragma once

#include "app_state.h"
#include "app_loxone_home_model.h"

/* Kép, kártya, jelszó és parancscél nélküli pillanatkép. A név és az ID csak
 * az egyetlen megjelenített javaslat azonosításához kell. */
typedef struct
{
    char id[LX_HOME_ID_SIZE];
    char name[LX_HOME_NAME_SIZE];
    double required_kw;
    double measured_kw;
    bool eligible;
    bool valid;
} app_assistant_consumer_t;

typedef struct
{
    app_assistant_consumer_t consumers[LX_HOME_MAX_CONSUMERS];
    unsigned count;
    unsigned revision;
    unsigned session;
    bool connected;
} app_assistant_consumers_t;

/* Azonos helyi óra 3..7 korábbi, igazoltan mért éjszakájának mediánja.
 * Előállítása a statisztikai RAM-tárból történik, SD I/O nélkül. */
typedef struct
{
    char date[11];
    unsigned hour;
    unsigned nights;
    double current_kw;
    double baseline_kw;
    bool ready;
} app_assistant_night_t;

typedef enum
{
    APP_ASSISTANT_NONE,
    APP_ASSISTANT_SOLAR,
    APP_ASSISTANT_NIGHT
} app_assistant_kind_t;

typedef struct
{
    app_assistant_kind_t kind;
    char id[LX_HOME_ID_SIZE];
    char name[LX_HOME_NAME_SIZE];
    double required_kw;
    double surplus_kw;
    double current_kw;
    double baseline_kw;
} app_assistant_result_t;

typedef struct
{
    int64_t since[LX_HOME_MAX_CONSUMERS];
    int64_t absent_since[LX_HOME_MAX_CONSUMERS];
    bool dismissed[LX_HOME_MAX_CONSUMERS];
    unsigned revision;
    unsigned session;
    unsigned selected_index;
    bool initialized;
    int64_t last_now;
    int64_t last_sample;
    int64_t quiet_until;
    int64_t night_since;
    char night_key[16];
    char dismissed_night[11];
    app_assistant_result_t result;
} app_assistant_t;

/* Csak javaslatot állít elő. Nincs automatikus kapcsolás, heap vagy flash-írás.
 * A hívó ugyanazon UI-lock alatt léptet és zár be. Invalid/stale/reconnect/
 * óra-visszalépés új, teljes tartóssági időt igényel. */
void app_assistant_update(app_assistant_t *assistant, int64_t now,
                          const app_energy_sample_t *sample, bool live,
                          const app_assistant_consumers_t *consumers,
                          const app_assistant_night_t *night);
void app_assistant_dismiss(app_assistant_t *assistant, int64_t now);
