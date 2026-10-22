/* A tényleges döntési modell, hálózat/LVGL/flash nélkül, monoton idővel. */
#include "app_energy_assistant.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static app_assistant_t state;
static app_assistant_consumers_t consumers;
static app_energy_sample_t sample;
static app_assistant_night_t night;
static int64_t now;

static void step(unsigned seconds)
{
    for (unsigned i = 0; i <= seconds / 5; ++i)
    {
        if (i)
            now += 5000000;
        sample.received_monotonic_us = now;
        app_assistant_update(&state, now, &sample, true, &consumers, &night);
    }
}

static void reset(void)
{
    memset(&state, 0, sizeof(state));
    memset(&consumers, 0, sizeof(consumers));
    memset(&night, 0, sizeof(night));
    now = 1000000;
    sample = (app_energy_sample_t){.power_pv_kw=5, .power_full_kw=1,
        .power_grid_kw=-4, .power_bat_kw=0, .soc_bat_pct=100, .status=1};
    consumers.connected = true;
    consumers.count = 3;
    consumers.revision = consumers.session = 1;
    for (unsigned i = 0; i < 3; ++i)
    {
        app_assistant_consumer_t *consumer = &consumers.consumers[i];
        snprintf(consumer->id, sizeof(consumer->id), "consumer-%u", i);
        snprintf(consumer->name, sizeof(consumer->name), "Mosógép %u", i);
        consumer->required_kw = 2;
        consumer->eligible = consumer->valid = true;
    }
}

int main(void)
{
    reset();
    step(115);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(5);
    assert(state.result.kind == APP_ASSISTANT_SOLAR);
    assert(!strcmp(state.result.id, "consumer-0") && state.result.required_kw == 2);
    consumers.consumers[0].measured_kw = .001;
    step(0);
    assert(state.result.kind == APP_ASSISTANT_SOLAR && !strcmp(state.result.id, "consumer-1"));
    app_assistant_dismiss(&state, now);
    step(1800);
    assert(state.result.kind == APP_ASSISTANT_SOLAR && !strcmp(state.result.id, "consumer-2"));
    assert(state.dismissed[1]);
    /* Bezárás után azonos feltétel nem adja vissza ugyanazt a fogyasztót. */
    consumers.count = 2;
    step(0);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    sample.soc_bat_pct = 99;
    step(600);
    sample.soc_bat_pct = 100;
    step(120);
    assert(state.result.kind == APP_ASSISTANT_SOLAR && !strcmp(state.result.id, "consumer-1"));

    /* Export, 100% SOC és PV önmagában kevés: akkuból exportálni nem PV-többlet. */
    for (unsigned scenario = 0; scenario < 15; ++scenario)
    {
        reset();
        switch (scenario)
        {
            case 0: sample.soc_bat_pct = 99.999; break;
            case 1: sample.soc_bat_pct = NAN; break;
            case 2: sample.power_bat_kw = .001; break;
            case 3: sample.power_pv_kw = .5; break;
            case 4: sample.power_grid_kw = 1; break;
            case 5: sample.power_grid_kw = -2.39; break;
            case 6: sample.power_bat_kw = -2; break;
            case 7: sample.night_mode = true; break;
            case 8: sample.status = 4; break;
            case 9: sample.power_pv_kw = INFINITY; break;
            case 10: sample.power_full_kw = -1; break;
            case 11: consumers.connected = false; break;
            case 12: consumers.count = LX_HOME_MAX_CONSUMERS + 1; break;
            case 13: sample.power_grid_kw = NAN; break;
            case 14: sample.status = 5; break;
        }
        step(300);
        assert(state.result.kind == APP_ASSISTANT_NONE);
    }
    reset();
    consumers.consumers[0].eligible = false;
    consumers.consumers[1].valid = false;
    consumers.consumers[2].required_kw = 0;
    step(300);
    assert(state.result.kind == APP_ASSISTANT_NONE);

    reset();
    step(120);
    app_assistant_update(&state, now + 10000001, &sample, true, &consumers, &night);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    now += 10000001;
    step(115);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(5);
    assert(state.result.kind == APP_ASSISTANT_SOLAR);
    consumers.session++;
    step(0);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(120);
    assert(state.result.kind == APP_ASSISTANT_SOLAR);
    consumers.revision++;
    step(0);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(120);
    app_assistant_update(&state, now, &sample, false, &consumers, &night);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(120);
    now -= 5000000;
    step(0);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(120);
    assert(state.result.kind == APP_ASSISTANT_NONE); /* Óra-visszalépéskor csendidő. */

    reset();
    sample.night_mode = true;
    sample.power_full_kw = .5;
    night = (app_assistant_night_t){.ready=true, .nights=3, .hour=0, .current_kw=.5, .baseline_kw=.2};
    strcpy(night.date, "2026-10-21");
    step(55);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    step(5);
    assert(state.result.kind == APP_ASSISTANT_NIGHT);
    app_assistant_dismiss(&state, now);
    step(3600);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    night.hour = 1;
    step(120);
    assert(state.result.kind == APP_ASSISTANT_NONE); /* Egy éjszaka egy bezárás. */
    strcpy(night.date, "2026-10-22");
    step(60);
    assert(state.result.kind == APP_ASSISTANT_NIGHT);
    sample.power_full_kw = .2;
    step(0);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    sample.power_full_kw = .5;
    night.nights = 2;
    step(120);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    night.nights = 3;
    night.baseline_kw = NAN;
    step(120);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    night.baseline_kw = .2;
    night.ready = false;
    step(120);
    assert(state.result.kind == APP_ASSISTANT_NONE);
    _Static_assert(sizeof(app_assistant_t) <= 512, "Az asszisztens túl nagy");
    printf("PASS: actual energy assistant, 120s PV/100%% SOC/zero load/margin, charge/discharge signs, hold reset, stale/session/revision/time, dismissal and comparable night rule; state=%zu bytes, snapshot=%zu bytes\n", sizeof(state), sizeof(consumers));
}
