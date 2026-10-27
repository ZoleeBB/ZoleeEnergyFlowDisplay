/* Hátralévő idő: a simítás mintánként egyszer lép, a 200 ms-os UI-kör csak olvassa. */
static app_battery_eta_state_t template_battery_eta_state;

/* Magyarul ragozva („6 óra 20 percig”, „45 percig”, „2 óráig”), angolul rövidítve. */
static void template_battery_duration(char *out, size_t size, const app_battery_eta_t *eta, bool until)
{
    unsigned hours = eta->minutes / 60, minutes = eta->minutes % 60;
    if (!hu())
    {
        if (eta->capped)
            snprintf(out, size, "over 99 h");
        else if (!hours)
            snprintf(out, size, "%u min", minutes);
        else if (!minutes)
            snprintf(out, size, "%u h", hours);
        else
            snprintf(out, size, "%u h %u min", hours, minutes);
        return;
    }
    if (eta->capped)
        snprintf(out, size, "%s", until ? "több mint 99 óráig" : "több mint 99 óra");
    else if (!hours)
        snprintf(out, size, until ? "%u percig" : "%u perc", minutes);
    else if (!minutes)
        snprintf(out, size, until ? "%u óráig" : "%u óra", hours);
    else
        snprintf(out, size, until ? "%u óra %u percig" : "%u óra %u perc", hours, minutes);
}

/* Egysoros, emberi nyelvű becslés. Szigetüzemben és áramszünetre készülve
 * a szöveg maga is jelzi a helyzetet; a kiemelő színt a hívó adja. */
static void template_battery_eta_text(char out[128], const app_energy_sample_t *sample)
{
    double nominal = s_cfg.energy.battery_capacity_kwh;
    app_battery_eta_update(&template_battery_eta_state, sample);
    app_battery_eta_t eta = app_battery_eta_estimate(&template_battery_eta_state, sample, nominal,
                                                     s_cfg.energy.battery_min_soc_pct);
    char duration[40], until[40];
    template_battery_duration(duration, sizeof(duration), &eta, false);
    template_battery_duration(until, sizeof(until), &eta, true);
    /* A ténylegesen használt határ látszik, akkor is, ha az alapértelmezés
     * (normál 15%, áramszünet/szigetüzem 5%) lépett életbe. */
    char floor_text[32] = "";
    if (eta.floor_pct > 0)
    {
        char number[16];
        fmt_dec(number, sizeof(number), eta.floor_pct, 0);
        snprintf(floor_text, sizeof(floor_text), hu() ? " (%s%%-ig)" : " (to %s%%)", number);
    }
    bool island = sample->status == 3;
    switch (eta.kind)
    {
    case APP_BATTERY_ETA_TO_FULL:
        snprintf(out, 128, hu() ? "%sKb. %s múlva tele" : "%sFull in about %s",
                 island ? tr("Szigetüzem · ", "Island mode · ") : "", duration);
        break;
    case APP_BATTERY_ETA_TO_EMPTY:
        snprintf(out, 128, hu() ? "%sMég kb. %s bírja%s" : "%sLasts about %s%s",
                 island ? tr("Szigetüzem · ", "Island mode · ") : "", hu() ? until : duration, floor_text);
        break;
    case APP_BATTERY_ETA_BACKUP:
        snprintf(out, 128, hu() ? "Áramszünetkor kb. %s bírná%s" : "In an outage about %s%s",
                 hu() ? until : duration, floor_text);
        break;
    case APP_BATTERY_ETA_FULL:
        snprintf(out, 128, "%s", tr("Az akkumulátor tele", "Battery is full"));
        break;
    case APP_BATTERY_ETA_EMPTY:
        snprintf(out, 128, "%s", tr("Elérte az alsó töltöttségi határt", "Reached the minimum charge level"));
        break;
    case APP_BATTERY_ETA_IDLE:
        snprintf(out, 128, "%s", tr("Készenlét: nincs töltés vagy kisütés", "Idle: not charging or discharging"));
        break;
    default:
        snprintf(out, 128, "%s", nominal <= 0 ? tr("Add meg a névleges kapacitást!", "Set nominal capacity first")
                                              : tr("Becslés az első mérés után", "Estimate after first sample"));
        break;
    }
}

/* Közös sablonadatok: az üres referencia mindig szöveg/—, sohasem kitalált nulla. */
static void template_battery_text(char out[128], const char *source,
                                  const app_energy_sample_t *sample,
                                  const app_stats_snapshot_t *stats)
{
    const app_battery_report_t *report = &stats->battery;
    double nominal = s_cfg.energy.battery_capacity_kwh;
    if (!strcmp(source, "battery_eta"))
    {
        template_battery_eta_text(out, sample);
        return;
    }
    if (!strcmp(source, "battery_diag_assessment"))
    {
        const char *message = nominal <= 0 ? tr("Add meg a névleges kapacitást!", "Set nominal capacity first") :
            !report->available ? tr("Helyi mérésre vár", "Waiting for local measurements") :
            !report->capacity_valid ? tr("Mérésgyűjtés: legalább 20 pp SOC / szakasz", "Collecting data: at least 20 pp SOC per run") :
            report->discharge_runs < 3 || report->broad_runs < 3 ? tr("Előzetes becslés: további nagy kisütések kellenek", "Preliminary: more broad discharge runs needed") :
            report->capacity_pct < 80 ? tr("Alacsony energiaarány: ellenőrzés javasolt", "Low energy ratio: check battery and measurement") :
            report->capacity_pct > 110 ? tr("A névleges érték / mérési pont ellenőrzendő", "Check nominal capacity and measurement point") :
            tr("A mért energiaarány megfelelő", "Measured energy ratio is in expected range");
        snprintf(out, 128, "%s", message);
        return;
    }
    if (!strcmp(source, "battery_diag_measurement"))
    {
        if (!report->available || !report->active_direction)
            snprintf(out, 128, "%s", tr("Várakozás folytonos töltési / kisütési szakaszra", "Waiting for continuous charge / discharge run"));
        else
        {
            char soc[24], energy[24];
            fmt_dec(soc, sizeof(soc), report->active_soc_span, 1);
            fmt_dec(energy, sizeof(energy), report->active_energy_kwh, 2);
            snprintf(out, 128, "%s: %s pp SOC / %s kWh", report->active_direction < 0 ?
                     tr("Élő töltés", "Live charge") : tr("Élő kisütés", "Live discharge"), soc, energy);
        }
        return;
    }
    if (!strcmp(source, "battery_diag_evidence"))
    {
        if (!report->capacity_valid)
            snprintf(out, 128, "%s", tr("Becslés, nem BMS-SOH. Kimaradáskor új szakasz indul.", "Estimate, not BMS SOH. Gaps start a new run."));
        else
        {
            time_t epoch = (time_t)report->latest_epoch_s;
            struct tm local;
            localtime_r(&epoch, &local);
            char date[24];
            strftime(date, sizeof(date), "%m.%d. %H:%M", &local);
            snprintf(out, 128, hu() ? "%u lezárt kisütés mediánja | utolsó: %s | becslés" :
                     "Median of %u discharge runs | latest: %s | estimate", report->discharge_runs, date);
        }
        return;
    }
    if (!strncmp(source, "battery_diag_day_", 17))
    {
        if (nominal <= 0)
            snprintf(out, 128, "--");
        else
        {
            double energy = !strcmp(source, "battery_diag_day_charge_pct") ?
                stats->totals.battery_charge_kwh : stats->totals.battery_discharge_kwh;
            char value[24];
            fmt_dec(value, sizeof(value), energy / nominal * 100, 1);
            snprintf(out, 128, hu() ? "A névleges %s%%-a (mért rész)" :
                     "%s%% of nominal (observed)", value);
        }
        return;
    }
    if (!report->available)
    {
        snprintf(out, 128, "--");
        return;
    }
    bool valid = true;
    double value = 0;
    const char *unit = "%";
    if (!strcmp(source, "battery_diag_capacity"))
    {
        valid = report->capacity_valid;
        value = report->capacity_kwh;
        unit = "kWh";
    }
    else if (!strcmp(source, "battery_diag_capacity_pct"))
    {
        valid = report->capacity_valid;
        value = report->capacity_pct;
    }
    else if (!strcmp(source, "battery_diag_charge_pct"))
    {
        valid = report->charge_valid;
        value = report->charge_ratio_pct;
    }
    else if (!strcmp(source, "battery_diag_discharge_pct"))
    {
        valid = report->discharge_valid;
        value = report->discharge_ratio_pct;
    }
    else if (!strcmp(source, "battery_diag_efficiency_pct"))
    {
        valid = report->efficiency_valid;
        value = report->efficiency_pct;
    }
    else if (!strcmp(source, "battery_diag_cycles"))
    {
        value = report->cycles;
        unit = "EFC";
    }
    else if (!strcmp(source, "battery_diag_c_rate"))
    {
        value = fabs(sample->power_bat_kw) / nominal;
        unit = "C";
    }
    else if (!strcmp(source, "battery_diag_soc_error"))
    {
        if (!report->active_ready)
            snprintf(out, 128, "%s", tr("SOC-eltérés: legalább 20 pp után", "SOC deviation: after at least 20 pp"));
        else
        {
            char number[24];
            fmt_dec(number, sizeof(number), report->active_soc_error_pp, 1);
            snprintf(out, 128, "SOC %s: %s%s pp", tr("eltérés", "deviation"),
                     report->active_soc_error_pp > 0 ? "+" : "", number);
        }
        return;
    }
    else if (!strcmp(source, "battery_diag_charge_detail") || !strcmp(source, "battery_diag_discharge_detail"))
    {
        bool charge = !strcmp(source, "battery_diag_charge_detail");
        valid = charge ? report->charge_valid : report->discharge_valid;
        if (valid)
        {
            char energy[24], soc[24];
            fmt_dec(energy, sizeof(energy), charge ? report->charge_energy_kwh : report->discharge_energy_kwh, 2);
            fmt_dec(soc, sizeof(soc), charge ? report->charge_soc_span : report->discharge_soc_span, 1);
            snprintf(out, 128, "%s kWh / %s pp SOC", energy, soc);
        }
        else
            snprintf(out, 128, "%s", tr("Legalább 20 pp / 10 perc szükséges", "At least 20 pp / 10 minutes required"));
        return;
    }
    if (!valid)
        snprintf(out, 128, "--");
    else
    {
        char number[24];
        fmt_dec(number, sizeof(number), value, !strcmp(unit, "C") || !strcmp(unit, "EFC") ? 2 : 1);
        bool live_ratio = report->active_ready &&
            ((!strcmp(source, "battery_diag_charge_pct") && report->active_direction < 0) ||
             (!strcmp(source, "battery_diag_discharge_pct") && report->active_direction > 0));
        snprintf(out, 128, "%s%s %s", live_ratio ? "~ " : "", number, unit);
    }
}
