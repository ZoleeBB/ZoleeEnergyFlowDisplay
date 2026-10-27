/* Passzív, a mérési ponton érvényes energia/SOC diagnosztika. Nem BMS-SOH mérés. */
#include "app_battery_diagnostics.h"
#include <math.h>
#include <string.h>

#define MIN_SPAN 20.0
#define MAX_AGE (30LL * 86400)

_Static_assert(sizeof(app_battery_diagnostics_t) <= 1024, "Az akku diagnosztika RAM-kerete 1 KiB");
_Static_assert(sizeof(app_battery_report_t) <= 256, "Az akku jelentés RAM-kerete 256 bájt");

static double span(const app_battery_run_t *run)
{
    return (run->start_soc - run->end_soc) * run->direction;
}

static bool ready(const app_battery_run_t *run, double nominal)
{
    return nominal > 0 && run->direction && span(run) >= MIN_SPAN - 1e-6 &&
           run->seconds >= 600 && run->energy_kwh >= nominal * .02;
}

void app_battery_init(app_battery_diagnostics_t *state, double nominal)
{
    memset(state, 0, sizeof(*state));
    state->nominal_kwh = isfinite(nominal) && nominal > 0 && nominal <= 1000 ? nominal : 0;
}

void app_battery_break(app_battery_diagnostics_t *state)
{
    state->have_previous = false;
    state->pair_charge_valid = false;
    state->active = (app_battery_run_t){0};
    state->idle_seconds = 0;
    if (state->interruptions < 1000000)
        state->interruptions++;
}

static void finish(app_battery_diagnostics_t *state)
{
    app_battery_run_t run = state->active;
    if (ready(&run, state->nominal_kwh))
    {
        if (state->count == APP_BATTERY_HISTORY)
        {
            memmove(state->history, state->history + 1,
                    (APP_BATTERY_HISTORY - 1) * sizeof(state->history[0]));
            state->count--;
        }
        state->history[state->count++] = run;
        /* Csak szomszédos töltés/kisütés, azonos SOC-ablak és folytonos adat. */
        if (run.direction > 0 && state->pair_charge_valid &&
            fabs(state->pair_charge.end_soc - run.start_soc) <= 1 &&
            fabs(state->pair_charge.start_soc - run.end_soc) <= 1 &&
            run.end_epoch_s - state->pair_charge.end_epoch_s <= 2 * 86400 &&
            run.energy_kwh <= state->pair_charge.energy_kwh)
        {
            state->efficiency_pct = 100 * run.energy_kwh / state->pair_charge.energy_kwh;
            state->efficiency_epoch_s = run.end_epoch_s;
            state->efficiency_valid = true;
        }
        state->pair_charge_valid = run.direction < 0;
        if (state->pair_charge_valid)
            state->pair_charge = run;
    }
    else if (run.direction)
        state->pair_charge_valid = false;
    state->active = (app_battery_run_t){0};
    state->idle_seconds = 0;
}

/* Előjelváltásnál a nullátmetszésen kettévágott, lineáris szakaszt kapunk. */
static void piece(app_battery_diagnostics_t *state, double a, double b,
                  double soc_a, double soc_b, double seconds, int64_t epoch)
{
    double average = (a + b) * .5;
    double energy = fabs(average) * seconds / 3600;
    if (average < 0)
        state->charge_kwh += energy;
    else
        state->discharge_kwh += energy;

    int direction = average < -.01 ? -1 : average > .01 ? 1 : 0;
    if (!direction)
    {
        /* A kis teljesítmény energiáját az összegzők mérik; kapacitásra nem alkalmas. */
        state->idle_seconds += seconds;
        if (state->active.direction &&
            (fabs(soc_b - state->active.end_soc) > 2 || state->idle_seconds >= 600))
            finish(state);
        if (fabs(soc_b - soc_a) > 2)
            app_battery_break(state);
        return;
    }
    state->idle_seconds = 0;
    if (state->active.direction && state->active.direction != direction)
        finish(state);
    app_battery_run_t *run = &state->active;
    if (!run->direction)
    {
        *run = (app_battery_run_t){.direction = direction, .start_soc = soc_a, .end_soc = soc_a};
        state->progress_soc = soc_a;
    }

    /* Több százalékpontos ellenirányú SOC-mozgás kalibrációt/hibás forrást jelez. */
    if ((soc_a - soc_b) * direction < -2 ||
        (state->progress_soc - soc_b) * direction < -2 || run->seconds + seconds > 30 * 86400)
    {
        app_battery_break(state);
        return;
    }
    run->end_soc = soc_b;
    if ((state->progress_soc - soc_b) * direction > 0)
        state->progress_soc = soc_b;
    run->energy_kwh += energy;
    run->seconds += seconds;
    run->end_epoch_s = epoch;
}

void app_battery_process(app_battery_diagnostics_t *state, const app_energy_sample_t *sample,
                         double nominal, unsigned timeout)
{
    if (!state || !sample)
        return;
    if (state->nominal_kwh != nominal)
        app_battery_init(state, nominal);
    if (!state->nominal_kwh)
        return;
    if (!isfinite(sample->power_bat_kw) || fabs(sample->power_bat_kw) > 15 ||
        !isfinite(sample->soc_bat_pct) || sample->soc_bat_pct < 0 || sample->soc_bat_pct > 100 ||
        sample->received_monotonic_us < 0 || sample->received_epoch_s < 1577836800)
    {
        app_battery_break(state);
        return;
    }
    if (state->have_previous)
    {
        const app_energy_sample_t *previous = &state->previous;
        double seconds = (sample->received_monotonic_us - previous->received_monotonic_us) / 1e6;
        double epoch_seconds = (double)(sample->received_epoch_s - previous->received_epoch_s);
        double allowed_jump = 2 + 3 * 15 * fmax(seconds, 0) / 3600 * 100 / nominal;
        if (seconds <= 0 || seconds > timeout || fabs(epoch_seconds - seconds) > 2 ||
            fabs(sample->soc_bat_pct - previous->soc_bat_pct) > allowed_jump)
            app_battery_break(state);
        else
        {
            double a = previous->power_bat_kw, b = sample->power_bat_kw;
            if (a * b < 0)
            {
                double fraction = a / (a - b);
                double mid_soc = previous->soc_bat_pct +
                                 fraction * (sample->soc_bat_pct - previous->soc_bat_pct);
                int64_t mid_epoch = previous->received_epoch_s + (int64_t)(seconds * fraction);
                piece(state, a, 0, previous->soc_bat_pct, mid_soc, seconds * fraction, mid_epoch);
                piece(state, 0, b, mid_soc, sample->soc_bat_pct, seconds * (1 - fraction),
                      sample->received_epoch_s);
            }
            else
                piece(state, a, b, previous->soc_bat_pct, sample->soc_bat_pct, seconds,
                      sample->received_epoch_s);
        }
    }
    state->previous = *sample;
    state->have_previous = true;
}

void app_battery_report(const app_battery_diagnostics_t *state, int64_t now,
                        app_battery_report_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!state || state->nominal_kwh <= 0)
        return;
    out->available = true;
    out->interruptions = state->interruptions;
    out->cycles = (state->charge_kwh + state->discharge_kwh) / (2 * state->nominal_kwh);
    out->active_direction = state->active.direction;
    out->active_soc_span = fmax(0, span(&state->active));
    out->active_energy_kwh = state->active.energy_kwh;
    out->active_soc_error_pp = out->active_soc_span -
                              state->active.energy_kwh / state->nominal_kwh * 100;
    out->active_ready = ready(&state->active, state->nominal_kwh);
    double values[APP_BATTERY_HISTORY];
    for (unsigned i = 0; i < state->count; i++)
    {
        const app_battery_run_t *run = &state->history[i];
        if (now < run->end_epoch_s || now - run->end_epoch_s > MAX_AGE)
            continue;
        double ratio = run->energy_kwh * 10000 / (span(run) * state->nominal_kwh);
        if (run->direction < 0)
        {
            out->charge_valid = true;
            out->charge_ratio_pct = ratio;
            out->charge_soc_span = span(run);
            out->charge_energy_kwh = run->energy_kwh;
        }
        else
        {
            out->discharge_valid = true;
            out->discharge_ratio_pct = ratio;
            out->discharge_soc_span = span(run);
            out->discharge_energy_kwh = run->energy_kwh;
            values[out->discharge_runs++] = run->energy_kwh * 100 / span(run);
            if (span(run) >= 60)
                out->broad_runs++;
            out->latest_epoch_s = run->end_epoch_s;
        }
    }
    if (out->discharge_runs)
    {
        for (unsigned i = 0; i < out->discharge_runs; i++)
            for (unsigned j = i + 1; j < out->discharge_runs; j++)
                if (values[j] < values[i])
                {
                    double swap = values[i];
                    values[i] = values[j];
                    values[j] = swap;
                }
        unsigned n = out->discharge_runs;
        out->capacity_kwh = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * .5;
        out->capacity_pct = out->capacity_kwh / state->nominal_kwh * 100;
        out->capacity_valid = true;
    }
    /* Az élő szakasz látható, de a mediánt csak lezárt mérések alkotják. */
    if (out->active_ready)
    {
        double ratio = state->active.energy_kwh * 10000 /
                       (span(&state->active) * state->nominal_kwh);
        if (state->active.direction < 0)
        {
            out->charge_valid = true;
            out->charge_ratio_pct = ratio;
            out->charge_soc_span = span(&state->active);
            out->charge_energy_kwh = state->active.energy_kwh;
        }
        else
        {
            out->discharge_valid = true;
            out->discharge_ratio_pct = ratio;
            out->discharge_soc_span = span(&state->active);
            out->discharge_energy_kwh = state->active.energy_kwh;
        }
    }
    out->efficiency_valid = state->efficiency_valid && now >= state->efficiency_epoch_s &&
                            now - state->efficiency_epoch_s <= MAX_AGE;
    out->efficiency_pct = state->efficiency_pct;
}

/* Verziózott, korlátos JSON; a RAM-beli előző mintát szándékosan nem mentjük. */
cJSON *app_battery_to_json(const app_battery_diagnostics_t *state)
{
    cJSON *json = cJSON_CreateObject();
    if (!json)
        return NULL;
    cJSON *runs = cJSON_AddArrayToObject(json, "runs");
    if (!runs || !cJSON_AddNumberToObject(json, "version", 1) ||
        !cJSON_AddNumberToObject(json, "nominal", state->nominal_kwh) ||
        !cJSON_AddNumberToObject(json, "charge", state->charge_kwh) ||
        !cJSON_AddNumberToObject(json, "discharge", state->discharge_kwh) ||
        !cJSON_AddNumberToObject(json, "efficiency", state->efficiency_valid ? state->efficiency_pct : 0) ||
        !cJSON_AddNumberToObject(json, "efficiency_epoch", state->efficiency_valid ? state->efficiency_epoch_s : 0))
        goto fail;
    for (unsigned i = 0; i < state->count; i++)
    {
        const app_battery_run_t *run = &state->history[i];
        cJSON *item = cJSON_CreateObject();
        if (!item)
            goto fail;
        if (!cJSON_AddItemToArray(runs, item))
        {
            cJSON_Delete(item);
            goto fail;
        }
        if (!cJSON_AddNumberToObject(item, "direction", run->direction) ||
            !cJSON_AddNumberToObject(item, "start", run->start_soc) ||
            !cJSON_AddNumberToObject(item, "end", run->end_soc) ||
            !cJSON_AddNumberToObject(item, "energy", run->energy_kwh) ||
            !cJSON_AddNumberToObject(item, "seconds", run->seconds) ||
            !cJSON_AddNumberToObject(item, "epoch", run->end_epoch_s))
            goto fail;
    }
    return json;
fail:
    cJSON_Delete(json);
    return NULL;
}

static bool read_number(const cJSON *json, const char *name, double low, double high, double *out)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble < low || value->valuedouble > high)
        return false;
    *out = value->valuedouble;
    return true;
}

bool app_battery_from_json(const cJSON *json, app_battery_diagnostics_t *out)
{
    app_battery_diagnostics_t parsed = {0};
    double version, efficiency_epoch;
    const cJSON *runs = cJSON_GetObjectItemCaseSensitive(json, "runs");
    if (!cJSON_IsObject(json) || !read_number(json, "version", 1, 1, &version) ||
        !read_number(json, "nominal", 0, 1000, &parsed.nominal_kwh) ||
        !read_number(json, "charge", 0, 1e9, &parsed.charge_kwh) ||
        !read_number(json, "discharge", 0, 1e9, &parsed.discharge_kwh) ||
        !read_number(json, "efficiency", 0, 100, &parsed.efficiency_pct) ||
        !read_number(json, "efficiency_epoch", 0, 4102444800., &efficiency_epoch) ||
        floor(efficiency_epoch) != efficiency_epoch || !cJSON_IsArray(runs) ||
        cJSON_GetArraySize(runs) > APP_BATTERY_HISTORY)
        return false;
    parsed.efficiency_epoch_s = (int64_t)efficiency_epoch;
    parsed.efficiency_valid = efficiency_epoch >= 1577836800;
    if ((efficiency_epoch != 0 && !parsed.efficiency_valid) ||
        (!parsed.efficiency_valid && parsed.efficiency_pct != 0))
        return false;
    const cJSON *item;
    cJSON_ArrayForEach(item, runs)
    {
        app_battery_run_t *run = &parsed.history[parsed.count];
        double direction, epoch;
        if (!read_number(item, "direction", -1, 1, &direction) || fabs(direction) != 1 ||
            !read_number(item, "start", 0, 100, &run->start_soc) ||
            !read_number(item, "end", 0, 100, &run->end_soc) ||
            !read_number(item, "energy", 0, 1e9, &run->energy_kwh) ||
            !read_number(item, "seconds", 600, 30 * 86400, &run->seconds) ||
            !read_number(item, "epoch", 1577836800, 4102444800., &epoch) || floor(epoch) != epoch)
            return false;
        run->direction = (int)direction;
        run->end_epoch_s = (int64_t)epoch;
        if (!ready(run, parsed.nominal_kwh) || run->energy_kwh > 15 * run->seconds / 3600 + 1e-6 ||
            (parsed.count && run->end_epoch_s < parsed.history[parsed.count - 1].end_epoch_s))
            return false;
        parsed.count++;
    }
    *out = parsed;
    return true;
}

void app_battery_eta_reset(app_battery_eta_state_t *state)
{
    if (state)
        memset(state, 0, sizeof(*state));
}

void app_battery_eta_update(app_battery_eta_state_t *state, const app_energy_sample_t *sample)
{
    if (!state || !sample || !isfinite(sample->power_bat_kw) || !isfinite(sample->power_full_kw))
        return;
    int64_t now = sample->received_monotonic_us;
    if (state->valid && now == state->last_us)
        return;
    double dt = state->valid ? (double)(now - state->last_us) / 1e6 : 0;
    /* Első minta, időugrás visszafelé vagy hosszú adatrés: tiszta újrakezdés,
     * a régi átlag nem húzhatja el az új szakasz becslését. */
    if (!state->valid || dt <= 0 || dt > APP_BATTERY_ETA_GAP_S)
    {
        state->battery_kw = sample->power_bat_kw;
        state->load_kw = sample->power_full_kw;
    }
    else
    {
        double alpha = 1.0 - exp(-dt / APP_BATTERY_ETA_TAU_S);
        state->battery_kw += alpha * (sample->power_bat_kw - state->battery_kw);
        state->load_kw += alpha * (sample->power_full_kw - state->load_kw);
    }
    state->valid = true;
    state->last_us = now;
}

static app_battery_eta_t eta_minutes(app_battery_eta_kind_t kind, double energy_kwh, double power_kw)
{
    app_battery_eta_t result = {.kind = kind};
    double minutes = power_kw > 0 ? energy_kwh / power_kw * 60.0 : INFINITY;
    if (!isfinite(minutes) || minutes > APP_BATTERY_ETA_MAX_MINUTES)
    {
        result.minutes = APP_BATTERY_ETA_MAX_MINUTES;
        result.capped = true;
    }
    else
        result.minutes = (unsigned)lround(fmax(1, minutes));
    return result;
}

app_battery_eta_t app_battery_eta_estimate(const app_battery_eta_state_t *state,
                                           const app_energy_sample_t *sample,
                                           double capacity_kwh, double min_soc_pct)
{
    app_battery_eta_t unknown = {.kind = APP_BATTERY_ETA_UNKNOWN};
    if (!state || !sample || !state->valid || !isfinite(capacity_kwh) || capacity_kwh <= 0 ||
        !isfinite(sample->soc_bat_pct))
        return unknown;
    double floor_pct = isfinite(min_soc_pct) ? fmin(90, fmax(0, min_soc_pct)) : 0;
    double soc = fmin(100, fmax(0, sample->soc_bat_pct));
    double usable_kwh = capacity_kwh * fmax(0, soc - floor_pct) / 100.0;
    /* Felkészülés áramszünetre: a ház mostani (simított) fogyasztásával
     * számolunk, mert kiesés után ezt az akkunak kell fedeznie. */
    if (sample->status == 2)
    {
        if (usable_kwh <= 0)
            return (app_battery_eta_t){.kind = APP_BATTERY_ETA_EMPTY};
        return eta_minutes(APP_BATTERY_ETA_BACKUP, usable_kwh, fmax(0, state->load_kw));
    }
    if (state->battery_kw > APP_BATTERY_ETA_IDLE_KW)
    {
        if (usable_kwh <= 0)
            return (app_battery_eta_t){.kind = APP_BATTERY_ETA_EMPTY};
        return eta_minutes(APP_BATTERY_ETA_TO_EMPTY, usable_kwh, state->battery_kw);
    }
    if (soc >= 99.5)
        return (app_battery_eta_t){.kind = APP_BATTERY_ETA_FULL};
    if (state->battery_kw < -APP_BATTERY_ETA_IDLE_KW)
        return eta_minutes(APP_BATTERY_ETA_TO_FULL, capacity_kwh * (100 - soc) / 100.0,
                           -state->battery_kw);
    return (app_battery_eta_t){.kind = APP_BATTERY_ETA_IDLE};
}
