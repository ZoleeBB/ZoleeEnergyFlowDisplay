#pragma once

#include "app_state.h"
#include "cJSON.h"

/* Korlátos, mintaszámtól független memória; az előzmény nem nyers mérésnapló. */
#define APP_BATTERY_HISTORY 7

typedef struct
{
    int direction; /* -1: töltés, +1: kisütés. */
    double start_soc, end_soc, energy_kwh, seconds;
    int64_t end_epoch_s;
} app_battery_run_t;

typedef struct
{
    double nominal_kwh;
    double charge_kwh, discharge_kwh; /* Csak a diagnosztikával megfigyelt időszak. */
    app_battery_run_t history[APP_BATTERY_HISTORY];
    unsigned count;
    double efficiency_pct;
    int64_t efficiency_epoch_s;
    bool efficiency_valid;

    /* Ezek RAM-állapotok: mentésből nem folytatunk félbeszakadt mérést. */
    bool have_previous, pair_charge_valid;
    app_energy_sample_t previous;
    app_battery_run_t active, pair_charge;
    double idle_seconds, progress_soc;
    unsigned interruptions;
} app_battery_diagnostics_t;

typedef struct
{
    bool available, active_ready, charge_valid, discharge_valid, capacity_valid, efficiency_valid;
    double charge_ratio_pct, discharge_ratio_pct, capacity_kwh, capacity_pct;
    double efficiency_pct, cycles, active_soc_span, active_energy_kwh, active_soc_error_pp;
    double charge_soc_span, discharge_soc_span, charge_energy_kwh, discharge_energy_kwh;
    unsigned discharge_runs, broad_runs, interruptions;
    int active_direction;
    int64_t latest_epoch_s;
} app_battery_report_t;

void app_battery_init(app_battery_diagnostics_t *state, double nominal_kwh);
void app_battery_break(app_battery_diagnostics_t *state);
void app_battery_process(app_battery_diagnostics_t *state, const app_energy_sample_t *sample,
                         double nominal_kwh, unsigned timeout_seconds);
void app_battery_report(const app_battery_diagnostics_t *state, int64_t now_epoch_s,
                        app_battery_report_t *out);
/* Csak lezárt eredmények kerülnek az atomi napi statisztikamentésbe. */
cJSON *app_battery_to_json(const app_battery_diagnostics_t *state);
bool app_battery_from_json(const cJSON *json, app_battery_diagnostics_t *out);

/*
 * Az akku hátralévő idejének becslése (töltésig / lemerülésig).
 * Csak a meglévő mintákból dolgozik: az akku- és a házteljesítményt
 * mintánként, a valódi eltelt idővel exponenciálisan simítja, így egy
 * pillanatnyi kiugrás nem ugráltatja a kijelzett időt. Nem foglal memóriát,
 * nem ír SD-t/flasht, és ugyanazt a mintát kétszer nem számolja.
 */
#define APP_BATTERY_ETA_TAU_S 180.0      /* Simítási időállandó. */
#define APP_BATTERY_ETA_GAP_S 120.0      /* Ennél hosszabb adatrésnél újrakezdés. */
#define APP_BATTERY_ETA_IDLE_KW 0.05     /* Ez alatt az akku készenlétben van. */
#define APP_BATTERY_ETA_MAX_MINUTES 5999 /* 99 ó 59 p; e fölött „több mint”. */
/*
 * Beállítatlan (0%) alsó határ esetén a hibridinverterek szokásos tartalékával
 * számolunk: normál üzemben a hálózati tartalék miatt 15% marad az akkuban,
 * áramszünetre készülve és szigetüzemben (bypass) viszont 5%-ig használható.
 */
#define APP_BATTERY_ETA_FLOOR_NORMAL_PCT 15.0
#define APP_BATTERY_ETA_FLOOR_BACKUP_PCT 5.0

typedef struct
{
    bool valid;
    double battery_kw; /* Simított; pozitív = kisütés, negatív = töltés. */
    double load_kw;    /* Simított házfogyasztás. */
    int64_t last_us;   /* Az utoljára feldolgozott minta monoton ideje. */
} app_battery_eta_state_t;

typedef enum
{
    APP_BATTERY_ETA_UNKNOWN = 0, /* Nincs kapacitás vagy még nincs minta. */
    APP_BATTERY_ETA_IDLE,        /* Nincs érdemi töltés/kisütés. */
    APP_BATTERY_ETA_FULL,        /* Tele (legalább 99,5%). */
    APP_BATTERY_ETA_TO_FULL,     /* Ennyi perc múlva tele. */
    APP_BATTERY_ETA_TO_EMPTY,    /* Ennyi percig bírja az alsó határig. */
    APP_BATTERY_ETA_EMPTY,       /* Elérte a beállított alsó határt. */
    APP_BATTERY_ETA_BACKUP,      /* Status 2: áramszünetkor ennyi ideig bírná. */
} app_battery_eta_kind_t;

typedef struct
{
    app_battery_eta_kind_t kind;
    unsigned minutes; /* Csak TO_FULL, TO_EMPTY és BACKUP esetén érvényes. */
    bool capped;      /* A valós érték nagyobb, mint APP_BATTERY_ETA_MAX_MINUTES. */
    double floor_pct; /* A számításhoz ténylegesen használt alsó határ. */
} app_battery_eta_t;

void app_battery_eta_reset(app_battery_eta_state_t *state);
/* Mintánként egyszer hívható; ugyanaz a minta (azonos monoton idő) nem számít újra. */
void app_battery_eta_update(app_battery_eta_state_t *state, const app_energy_sample_t *sample);
/*
 * capacity_kwh <= 0 esetén UNKNOWN. A min_soc_pct az inverter/BMS alsó határa
 * (0–90). 0 (beállítatlan) esetén a fenti alapértelmezések lépnek életbe:
 * normál üzemben 15%, áramszünetre készülve és szigetüzemben 5%.
 */
app_battery_eta_t app_battery_eta_estimate(const app_battery_eta_state_t *state,
                                           const app_energy_sample_t *sample,
                                           double capacity_kwh, double min_soc_pct);
