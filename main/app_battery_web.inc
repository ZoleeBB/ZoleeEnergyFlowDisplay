/* Hitelesített akkumulátor-diagnosztika és külön, szándékos nullázás. */
static esp_err_t battery_get(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    app_stats_battery_t state;
    bool local = app_stats_get_battery(&state);
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Nincs memória / Out of memory");
    cJSON_AddBoolToObject(root, "local", local);
    cJSON_AddBoolToObject(root, "available", local && state.report.available);
    cJSON_AddNumberToObject(root, "nominal_kwh", state.nominal_kwh);
    cJSON_AddNumberToObject(root, "charge_kwh", state.charge_kwh);
    cJSON_AddNumberToObject(root, "discharge_kwh", state.discharge_kwh);
    cJSON_AddNumberToObject(root, "cycles", state.report.cycles);
    cJSON_AddNumberToObject(root, "runs", state.report.discharge_runs);
    cJSON_AddNumberToObject(root, "active_direction", state.report.active_direction);
    cJSON_AddNumberToObject(root, "active_soc_span", state.report.active_soc_span);
    if (state.report.capacity_valid) {
        cJSON_AddNumberToObject(root, "capacity_kwh", state.report.capacity_kwh);
        cJSON_AddNumberToObject(root, "capacity_pct", state.report.capacity_pct);
    }
    if (state.report.efficiency_valid)
        cJSON_AddNumberToObject(root, "efficiency_pct", state.report.efficiency_pct);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Nincs memória / Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t error = httpd_resp_sendstr(req, json);
    free(json);
    return error;
}

static esp_err_t battery_reset_post(httpd_req_t *req)
{
    if (!authorize(req))
        return ESP_OK;
    char header[8];
    if (req->content_len || httpd_req_get_hdr_value_str(req, "X-Zolee-Battery", header, sizeof(header)) != ESP_OK
        || strcmp(header, "1"))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Érvénytelen nullázási kérés / Invalid reset request");
    if (app_stats_reset_battery() != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
            s_hu ? "Az akkumulátor-statisztika nem nullázható/menthető. Ellenőrizd az SD-kártyát, majd próbáld újra."
                 : "Battery statistics could not be reset/saved. Check the SD card and retry.");
    app_events_add(APP_EVENT_INFO, "Akkumulátor diagnosztikai statisztika nullázva.");
    return httpd_resp_sendstr(req, "OK");
}

static void battery_section(httpd_req_t *req)
{
    httpd_resp_send_chunk(req,
        "<section class='card' id='battery-statistics'><h2 id='battery-title'></h2>"
        "<p id='battery-note'></p><table id='battery-values'></table>"
        "<button type='button' id='battery-reset' disabled></button>"
        "<p id='battery-feedback' role='status' aria-live='polite'></p></section>"
        "<script>(()=>{const hu=" , HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, s_hu ? "true" : "false", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req,
        ";const text=(a,b)=>hu?a:b,$=id=>document.getElementById(id),button=$('battery-reset');"
        "$('battery-title').textContent=text('Akkumulátor-statisztika','Battery statistics');"
        "button.textContent=text('Akkumulátor-statisztika nullázása','Reset battery statistics');"
        "let pending=false,busy=false,local=false;"
        "const number=(v,d=2)=>typeof v==='number'&&Number.isFinite(v)?v.toLocaleString(hu?'hu-HU':'en-US',{maximumFractionDigits:d}):'—';"
        "async function refresh(){if(pending)return;pending=true;try{"
        "const r=await fetch('/api/battery',{cache:'no-store'});if(!r.ok)throw Error(await r.text());const s=await r.json();local=s.local;"
        "$('battery-note').textContent=!s.local?text('A diagnosztika a Master kijelzőn érhető el.','Diagnostics are available on the Master display.'):"
        "!s.available?text('Állítsd be az akkumulátor névleges kapacitását az energiabeállításokban.','Set the nominal battery capacity in energy settings.'):"
        "text('A megfigyelt időszak adatai. A kapacitás és a hatásfok becslés; elegendő mérésig nincs érték.','Observed period only. Capacity and efficiency are estimates; values require sufficient measurements.');"
        "const rows=[[text('Névleges kapacitás','Nominal capacity'),number(s.nominal_kwh)+' kWh'],"
        "[text('Megfigyelt töltés','Observed charge'),number(s.charge_kwh)+' kWh'],"
        "[text('Megfigyelt kisütés','Observed discharge'),number(s.discharge_kwh)+' kWh'],"
        "[text('Egyenértékű ciklusok','Equivalent cycles'),number(s.cycles,3)],"
        "[text('Becsült kapacitás','Estimated capacity'),number(s.capacity_kwh)+' kWh / '+number(s.capacity_pct,1)+' %'],"
        "[text('Becsült hatásfok','Estimated efficiency'),number(s.efficiency_pct,1)+' %'],"
        "[text('Értékelt kisütési szakaszok','Evaluated discharge runs'),number(s.runs,0)],"
        "[text('Aktuális mérési szakasz','Current measurement'),s.active_direction<0?text('Töltés','Charging'):s.active_direction>0?text('Kisütés','Discharging'):text('Várakozás','Waiting')]];"
        "const table=$('battery-values');table.replaceChildren();for(const [label,value] of rows){const row=document.createElement('tr');"
        "for(const v of [label,s.local?value:'—']){const cell=document.createElement('td');cell.textContent=v;row.append(cell);}table.append(row);}"
        "}catch(e){$('battery-feedback').textContent=text('Lekérési hiba: ','Load error: ')+e.message;}finally{pending=false;button.disabled=busy||!local;}}"
        "button.onclick=async()=>{if(busy||!confirm(text('Törlöd az akkumulátor eddigi diagnosztikai statisztikáját? A gyűjtés újrakezdődik. A napi energiagrafikonok megmaradnak.','Reset all battery diagnostic statistics and start collecting again? Daily energy charts are preserved.')))return;"
        "busy=true;button.disabled=true;try{const r=await fetch('/api/battery/reset',{method:'POST',headers:{'X-Zolee-Battery':'1'}});if(!r.ok)throw Error(await r.text());"
        "$('battery-feedback').textContent=text('A statisztika nullázva és elmentve. Az adatgyűjtés újraindult.','Statistics reset and saved. Collection restarted.');await refresh();"
        "}catch(e){$('battery-feedback').textContent=e.message;}finally{busy=false;button.disabled=!local;}};refresh();setInterval(refresh,10000);})();</script>",
        HTTPD_RESP_USE_STRLEN);
}
