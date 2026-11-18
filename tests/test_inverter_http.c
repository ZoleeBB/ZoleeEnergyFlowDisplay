#include "app_inverter_http_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned call, count;
static const char *uris[5], *replies[5], *expect_pem, *expect_body;
static bool expect_sign;
static char solax_reply[1500];

time_t __wrap_time(time_t *out)
{
    const time_t now = 1774176450;
    if (out) *out = now;
    return now;
}

static const char *header(const char *const headers[][2], size_t n, const char *name)
{
    for (size_t i = 0; i < n; i++) if (!strcmp(headers[i][0], name)) return headers[i][1];
    return "";
}

cJSON *app_inverter_json_request(const char *url, const char *method, const char *body,
                                 const char *const headers[][2], size_t n, char *token,
                                 size_t token_size, const char *pem)
{
    assert(call < count && strstr(url, uris[call]));
    assert(!strcmp(method, body ? "POST" : "GET"));
    if (expect_body) assert(body && strstr(body, expect_body));
    const char *meter = strstr(url, "/getdevdata.cgi?device=3");
    if (meter) assert(!strcmp(meter, "/getdevdata.cgi?device=3"));
    if (expect_pem) assert(pem && !strcmp(pem, expect_pem));
    if (strstr(url, "login")) {
        assert(strstr(body, "\"systemCode\":\"test-secret\""));
        assert(token && token_size > 8);
        strcpy(token, "session");
    }
    if (strstr(url, "getDevHistoryKpi")) {
        assert(strstr(body, "\"devTypeId\":38"));
        assert(!strcmp(header(headers, n, "XSRF-TOKEN"), "session"));
    }
    if (expect_sign) assert(!strcmp(header(headers, n, "X-Ca-Signature"),
                                     "1IsiPfn9+I1seDGmTsfAk79DfY2zBO+M3b36AAtjRck="));
    const char *response = replies[call++];
    return response ? cJSON_Parse(response) : NULL;
}

static void reset(void)
{
    call = count = 0;
    expect_pem = expect_body = NULL;
    expect_sign = false;
}

static void queue(const char *uri, const char *reply)
{
    assert(count < 5);
    uris[count] = uri;
    replies[count++] = reply;
}

static void done(void)
{
    assert(call == count);
    reset();
}

static void near(double actual, double expected)
{
    assert(fabs(actual - expected) < 1e-9);
}

int main(void)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    assert(inv_http_timestamp("2026-03-22T11:47:30.123+01:00", false) == 1774176450);
    assert(!inv_http_timestamp("2026-02-29 11:47:30", true));
    assert(!inv_http_timestamp("2024-02-29 11:47:30 BAD", true));
    app_energy_sample_t s = {0};
    app_inverter_config_t cfg = {.profile="fronius_http", .host="inverter.local", .battery=true};
    int64_t t = -1;
    queue("http://inverter.local:80/solar_api/v1/GetPowerFlowRealtimeData.fcgi",
          "{\"Head\":{\"Status\":{\"Code\":0}},\"Body\":{\"Data\":{\"Site\":{\"P_PV\":4500,\"P_Load\":-3000,"
          "\"P_Akku\":-500,\"P_Grid\":-1000},\"Inverters\":{\"1\":{\"SOC\":0}}}}}");
    assert(app_inverter_http_read(&cfg, &s, &t) && !s.soc_unavailable && s.soc_bat_pct == 0 && t == 0);
    near(s.power_grid_kw, -1);
    near(s.power_bat_kw, -0.5);
    near(s.power_full_kw, 3);
    assert(!s.battery_temperature_valid);
    done();

    strcpy(cfg.profile, "solplanet_http");
    strcpy(cfg.device, "ASW8000T");
    cfg.battery = false;
    queue("getdevdata.cgi?device=2&sn=ASW8000T", NULL);
    queue("invdata.cgi?sn=ASW8000T", "{\"pac\":3000,\"err\":0,\"tmp\":345}");
    queue("emeter.cgi", "{}");
    assert(app_inverter_http_read(&cfg, &s, &t) && s.grid_unavailable && s.home_unavailable && s.battery_unavailable);
    near(s.power_pv_kw, 3);
    done();

    strcpy(cfg.profile, "solplanet_https");
    expect_pem = cfg.trusted_pem = "logger-cert";
    cfg.battery = true;
    queue("https://inverter.local:443/getdevdata.cgi?device=2", "{\"pac\":3200,\"vpv\":[4000],\"ipv\":[1000]}");
    queue("device=3", "{\"pac\":-1000}");
    queue("device=4", "{\"ppv\":4000,\"pb\":-800,\"soc\":51,\"tb\":0}");
    assert(app_inverter_http_read(&cfg, &s, &t) && s.battery_temperature_valid && s.battery_temperature_c == 0);
    near(s.power_full_kw, 2.2);
    near(s.power_bat_kw, -0.8);
    near(s.power_pv_kw, 4);
    done();
    queue("device=2", "{\"pac\":2500,\"vpv\":[5000],\"ipv\":[500]}");
    queue("device=3", "{}");
    queue("device=4", "{\"cst\":0,\"bst\":0,\"vb\":0,\"tb\":0,\"soc\":0,\"soh\":0,\"cli\":0,\"clo\":0,\"ppv\":0,\"pb\":0}");
    assert(app_inverter_http_read(&cfg, &s, &t) && s.soc_unavailable && !s.battery_temperature_valid);
    assert(s.battery_power_unavailable && s.grid_unavailable);
    near(s.power_pv_kw, 2.5);
    done();
    cfg.trusted_pem = NULL;

    strcpy(cfg.profile, "solax_http");
    strcpy(cfg.secret, "pw&a");
    char *p = solax_reply;
    p += sprintf(p, "{\"type\":14,\"Data\":[");
    for (int i = 0; i < 200; i++)
        p += sprintf(p, "%s%d", i ? "," : "", i == 14 ? 2000 : i == 15 ? 500 : i == 34 ? 64536 :
                      i == 35 ? 65535 : i == 41 ? 65530 : i == 103 ? 99 : 0);
    strcpy(p, "]}");
    expect_body = "pwd=pw%26a";
    queue("http://inverter.local:80/", solax_reply);
    assert(app_inverter_http_read(&cfg, &s, &t));
    near(s.power_pv_kw, 2.5);
    near(s.power_grid_kw, 1);
    near(s.power_bat_kw, 0.006);
    done();

    strcpy(cfg.profile, "fronius_cloud");
    strcpy(cfg.host, "api.solarweb.com");
    strcpy(cfg.device, "04D81B82-7861-4E36-8E7F-41036CE711A4");
    queue("pvsystems/04D81B82-7861-4E36-8E7F-41036CE711A4/flowdata", "{\"pvSystemId\":\"04d81b82-7861-4e36-8e7f-41036ce711a4\",\"status\":{\"isOnline\":true},"
          "\"data\":{\"logDateTime\":\"2026-03-22T10:47:30Z\",\"channels\":["
          "{\"channelName\":\"PowerPV\",\"unit\":\"W\",\"value\":3700},"
          "{\"channelName\":\"PowerFeedIn\",\"unit\":\"W\",\"value\":-1100},"
          "{\"channelName\":\"PowerLoad\",\"unit\":\"W\",\"value\":-2700},"
          "{\"channelName\":\"PowerBattCharge\",\"unit\":\"W\",\"value\":null}]}}");
    assert(app_inverter_http_read(&cfg, &s, &t) && t == 1774176450 && s.battery_power_unavailable);
    near(s.power_full_kw, 2.7);
    near(s.power_grid_kw, -1.1);
    done();
    queue("pvsystems/04D81B82-7861-4E36-8E7F-41036CE711A4/flowdata", "{\"pvSystemId\":\"04d81b82-7861-4e36-8e7f-41036ce711a4\",\"status\":{\"isOnline\":true},"
          "\"data\":{\"logDateTime\":\"2026-03-22T10:47:30Z\",\"channels\":["
          "{\"channelName\":\"PowerPV\",\"unit\":\"W\",\"value\":4057.5},"
          "{\"channelName\":\"PowerBattCharge\",\"unit\":\"W\",\"value\":-2521.10},"
          "{\"channelName\":\"BattSOC\",\"unit\":\"%\",\"value\":99}]}}");
    assert(app_inverter_http_read(&cfg, &s, &t) && !s.battery_power_unavailable && !s.soc_unavailable);
    near(s.power_bat_kw, -2.5211);
    near(s.soc_bat_pct, 99);
    done();
    const char *meterless = "{\"pvSystemId\":\"04d81b82-7861-4e36-8e7f-41036ce711a4\",\"status\":{\"isOnline\":true},"
        "\"data\":{\"logDateTime\":\"2026-03-22T10:47:30Z\",\"channels\":["
        "{\"channelName\":\"PowerOutput\",\"unit\":\"W\",\"value\":1041}]}}";
    cfg.battery = false;
    queue("pvsystems/04D81B82-7861-4E36-8E7F-41036CE711A4/flowdata", meterless);
    assert(app_inverter_http_read(&cfg, &s, &t) && s.home_unavailable && s.grid_unavailable);
    near(s.power_pv_kw, 1.041);
    done();
    cfg.battery = true;
    queue("pvsystems/04D81B82-7861-4E36-8E7F-41036CE711A4/flowdata", meterless);
    assert(!app_inverter_http_read(&cfg, &s, &t));
    done();
    strcpy(cfg.host, "fakesolarweb.com");
    assert(!app_inverter_http_read(&cfg, &s, &t));
    done();

    strcpy(cfg.profile, "solax_cloud");
    strcpy(cfg.host, "global.solaxcloud.com");
    strcpy(cfg.device, "WIFI");
    expect_body = "{\"wifiSn\":\"WIFI\"}";
    queue("/api/v2/dataAccess/realtimeInfo/get", "{\"success\":true,\"result\":{\"sn\":\"WIFI\","
          "\"utcDateTime\":\"2026-03-22T10:47:30Z\",\"powerdc1\":3100,\"powerdc2\":null,"
          "\"feedinpower\":1000,\"acpower\":2600,\"batPower\":500,\"inverterStatus\":\"102\"}}");
    assert(app_inverter_http_read(&cfg, &s, &t) && t == 1774176450 && s.soc_unavailable);
    near(s.power_bat_kw, -0.5);
    near(s.power_full_kw, 1.6);
    done();

    strcpy(cfg.profile, "huawei_cloud");
    strcpy(cfg.host, "eu5.fusionsolar.huawei.com");
    strcpy(cfg.secret, "test-secret");
    strcpy(cfg.device, "HUAWEI1");
    const char *huawei = "{\"success\":true,\"data\":[{\"sn\":\"HUAWEI1\",\"collectTime\":1774176300000,"
                         "\"dataItemMap\":{\"mppt_power\":2.25}},{\"collectTime\":1774176000000,"
                         "\"dataItemMap\":{\"mppt_power\":0.15}}]}";
    queue("/thirdData/login", "{\"success\":true}");
    queue("/thirdData/getDevHistoryKpi", huawei);
    assert(app_inverter_http_read(&cfg, &s, &t) && t == 1774176300 && s.grid_unavailable);
    near(s.power_pv_kw, 2.25);
    done();
    queue("/thirdData/getDevHistoryKpi", "{\"success\":false,\"failCode\":305}");
    assert(!app_inverter_http_read(&cfg, &s, &t));
    done();
    queue("/thirdData/login", "{\"success\":true}");
    queue("/thirdData/getDevHistoryKpi", huawei);
    assert(app_inverter_http_read(&cfg, &s, &t));
    app_inverter_http_reset();
    done();

    strcpy(cfg.profile, "solplanet_cloud");
    strcpy(cfg.host, "eu-api-genergal.aisweicloud.com");
    strcpy(cfg.key, "plant123");
    strcpy(cfg.user, "123456789");
    expect_sign = true;
    queue("/getPlantOutput?date=2026-03-22&key=plant123&period=bydays", "{\"dataunit\":\"W\",\"data\":["
          "{\"time\":\"10:40\",\"value\":\"725\"},{\"time\":\"11:00\",\"value\":0},{\"time\":\"10:20\",\"value\":\"n/a\"}]}");
    assert(app_inverter_http_read(&cfg, &s, &t) && t == 1774176000 && s.grid_unavailable);
    near(s.power_pv_kw, 0.725);
    done();
    puts("PASS: nyolc HTTP inverterprofil, mérési idő, aláírás, újrahitelesítés és hiányzó adatok");
}
