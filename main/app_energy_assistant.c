/* Helyi, determinisztikus szabályok; minden teljesítmény kW, az idő monoton µs.
 * Grid: negatív az export. Akku: pozitív a kisütés, negatív a töltés. */
#include "app_energy_assistant.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SOLAR_HOLD_US INT64_C(120000000)
#define NIGHT_HOLD_US INT64_C(60000000)
#define SAMPLE_FRESH_US INT64_C(10000000)
#define DISMISS_QUIET_US INT64_C(1800000000)
#define EPISODE_RESET_US INT64_C(600000000)

static int64_t quiet_deadline(int64_t now)
{
    return now > INT64_MAX - DISMISS_QUIET_US ? INT64_MAX : now + DISMISS_QUIET_US;
}

static void reset_candidates(app_assistant_t *assistant)
{
    memset(assistant->since, 0, sizeof(assistant->since));
    assistant->night_since = 0;
}

static bool solar_power(const app_energy_sample_t *sample, double *surplus)
{
    if (sample->night_mode || sample->status != 1 || sample->soc_bat_pct != 100 ||
        !isfinite(sample->power_pv_kw) || !isfinite(sample->power_full_kw) ||
        !isfinite(sample->power_bat_kw) || !isfinite(sample->power_grid_kw) ||
        sample->power_pv_kw <= 0 || sample->power_full_kw < 0 ||
        sample->power_bat_kw > 0 || sample->power_grid_kw >= 0)
        return false;
    double pv_remaining = sample->power_pv_kw - sample->power_full_kw -
                          fmax(0, -sample->power_bat_kw);
    *surplus = fmin(-sample->power_grid_kw, pv_remaining);
    return *surplus > 0;
}

void app_assistant_update(app_assistant_t *assistant, int64_t now,
                          const app_energy_sample_t *sample, bool live,
                          const app_assistant_consumers_t *consumers,
                          const app_assistant_night_t *night)
{
    if (!assistant)
        return;
    assistant->result = (app_assistant_result_t){0};
    bool fresh = live && sample && consumers && consumers->connected &&
                 consumers->count <= LX_HOME_MAX_CONSUMERS && now > 0 &&
                 sample->received_monotonic_us > 0 &&
                 now >= sample->received_monotonic_us &&
                 now - sample->received_monotonic_us <= SAMPLE_FRESH_US;
    if (!fresh)
    {
        reset_candidates(assistant);
        /* Kapcsolati adathiány nem bizonyítja egy bezárt epizód végét. */
        memset(assistant->absent_since, 0, sizeof(assistant->absent_since));
        assistant->last_sample = 0;
        return;
    }
    bool clock_reset = assistant->last_now && now < assistant->last_now;
    if (!assistant->initialized || assistant->revision != consumers->revision)
    {
        reset_candidates(assistant);
        memset(assistant->dismissed, 0, sizeof(assistant->dismissed));
        memset(assistant->absent_since, 0, sizeof(assistant->absent_since));
        assistant->revision = consumers->revision;
        assistant->initialized = true;
    }
    if (clock_reset || assistant->session != consumers->session ||
        (assistant->last_sample && (sample->received_monotonic_us < assistant->last_sample ||
         sample->received_monotonic_us - assistant->last_sample > SAMPLE_FRESH_US)))
    {
        reset_candidates(assistant);
        memset(assistant->absent_since, 0, sizeof(assistant->absent_since));
    }
    if (clock_reset)
        assistant->quiet_until = quiet_deadline(now);
    assistant->session = consumers->session;
    assistant->last_now = now;
    assistant->last_sample = sample->received_monotonic_us;
    double surplus = 0;
    bool solar = solar_power(sample, &surplus);
    int selected = -1;
    for (unsigned i = 0; i < consumers->count; ++i)
    {
        const app_assistant_consumer_t *consumer = &consumers->consumers[i];
        double margin = fmax(.1, consumer->required_kw * .2);
        bool known = consumer->valid && isfinite(consumer->measured_kw) && consumer->measured_kw >= 0;
        bool suitable = solar && consumer->eligible && known &&
                        consumer->id[0] && consumer->name[0] &&
                        isfinite(consumer->required_kw) && consumer->required_kw > 0 &&
                        consumer->measured_kw == 0 && surplus >= consumer->required_kw + margin;
        if (!suitable)
        {
            assistant->since[i] = 0;
            if (!known)
            {
                assistant->absent_since[i] = 0;
                continue;
            }
            if (!assistant->absent_since[i])
                assistant->absent_since[i] = now;
            if (now - assistant->absent_since[i] >= EPISODE_RESET_US)
                assistant->dismissed[i] = false;
            continue;
        }
        assistant->absent_since[i] = 0;
        if (!assistant->since[i])
            assistant->since[i] = now;
        if (selected < 0 && !assistant->dismissed[i] && now - assistant->since[i] >= SOLAR_HOLD_US)
            selected = (int)i;
    }
    if (selected >= 0 && now >= assistant->quiet_until)
    {
        const app_assistant_consumer_t *consumer = &consumers->consumers[selected];
        assistant->selected_index = (unsigned)selected;
        assistant->result.kind = APP_ASSISTANT_SOLAR;
        snprintf(assistant->result.id, sizeof(assistant->result.id), "%s", consumer->id);
        snprintf(assistant->result.name, sizeof(assistant->result.name), "%s", consumer->name);
        assistant->result.required_kw = consumer->required_kw;
        assistant->result.surplus_kw = surplus;
    }
    bool unusual = sample->night_mode && night && night->ready && night->nights >= 3 &&
                   night->date[0] && night->hour < 4 &&
                   isfinite(night->baseline_kw) && night->baseline_kw >= 0 &&
                   isfinite(night->current_kw) && night->current_kw >= 0 &&
                   isfinite(sample->power_full_kw) &&
                   night->current_kw >= fmax(night->baseline_kw * 1.5, night->baseline_kw + .1) &&
                   sample->power_full_kw >= fmax(night->baseline_kw * 1.5, night->baseline_kw + .1);
    char key[16] = "";
    if (unusual)
        snprintf(key, sizeof(key), "%.10s-%u", night->date, night->hour);
    if (!unusual || strcmp(key, assistant->night_key))
    {
        assistant->night_since = 0;
        snprintf(assistant->night_key, sizeof(assistant->night_key), "%s", key);
    }
    if (unusual && !assistant->night_since)
        assistant->night_since = now;
    if (unusual && now - assistant->night_since >= NIGHT_HOLD_US &&
        now >= assistant->quiet_until && strcmp(night->date, assistant->dismissed_night))
    {
        assistant->result.kind = APP_ASSISTANT_NIGHT;
        assistant->result.current_kw = night->current_kw;
        assistant->result.baseline_kw = night->baseline_kw;
    }
}

void app_assistant_dismiss(app_assistant_t *assistant, int64_t now)
{
    if (!assistant || assistant->result.kind == APP_ASSISTANT_NONE)
        return;
    if (assistant->result.kind == APP_ASSISTANT_SOLAR &&
        assistant->selected_index < LX_HOME_MAX_CONSUMERS)
        assistant->dismissed[assistant->selected_index] = true;
    if (assistant->result.kind == APP_ASSISTANT_NIGHT)
        snprintf(assistant->dismissed_night, sizeof(assistant->dismissed_night), "%.10s", assistant->night_key);
    assistant->quiet_until = quiet_deadline(now);
    assistant->result = (app_assistant_result_t){0};
}
