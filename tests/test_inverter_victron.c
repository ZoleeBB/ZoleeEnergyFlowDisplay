#include "app_inverter_driver.h"
#include "app_inverter_victron_model.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mqtt_client { event_fn fn; void *ctx; };
static int subscriptions, starts, stops, captures, beats;
static bool reply_heartbeat = true, wrong_echo, partial_pv, retained_only;
static struct mqtt_client *active;
static const char *prefix = "N/a1b2c3d4e5f6/";
static bool tls, battery;
static int64_t now = 1000;
uint32_t esp_random(void) { static uint32_t sequence; return ++sequence; }
int64_t esp_timer_get_time(void) { return now; }
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return 0; }
EventGroupHandle_t xEventGroupCreate(void) { return calloc(1, sizeof(EventBits_t)); }
void vEventGroupDelete(EventGroupHandle_t g) { free(g); }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t m, int c, int a, unsigned t)
{
    (void)m; (void)c; (void)a; (void)t; return *g;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t b) { return *g |= b; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t b) { return *g &= ~b; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, sizeof(pthread_mutex_t)); }
int xSemaphoreTake(SemaphoreHandle_t m, unsigned t) { (void)t; return m != NULL; }
int xSemaphoreGive(SemaphoreHandle_t m) { return m != NULL; }
void vSemaphoreDelete(SemaphoreHandle_t m) { free(m); }

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *cfg)
{
    assert((cfg->broker.address.transport == MQTT_TRANSPORT_OVER_SSL) == tls);
    if (tls) assert(!strcmp(cfg->broker.verification.certificate, "trusted-pem"));
    assert(cfg->network.disable_auto_reconnect);
    assert(!strcmp(cfg->broker.address.hostname,"gx.local"));
    assert(!active);
    active = calloc(1, sizeof(*active));
    return active;
}

esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c, int id, event_fn fn, void *arg)
{
    (void)id; c->fn = fn; c->ctx = arg; return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c)
{
    starts++; c->fn(c->ctx, NULL, MQTT_EVENT_CONNECTED, NULL); return ESP_OK;
}
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t c)
{
    stops++; c->fn(c->ctx, NULL, MQTT_EVENT_DISCONNECTED, NULL); return ESP_OK;
}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t c)
{
    assert(c == active); free(c); active = NULL; return ESP_OK;
}
int esp_mqtt_client_subscribe_multiple(esp_mqtt_client_handle_t c, const esp_mqtt_topic_t *t, int n)
{
    subscriptions++;
    assert(n == (battery ? 8 : 5));
    for (int i = 0; i < n; i++) {
        assert(!strncmp(t[i].filter, prefix, strlen(prefix)));
        if (!battery) assert(!strstr(t[i].filter, "Battery"));
    }
    esp_mqtt_event_t event = {0};
    c->fn(c->ctx, NULL, MQTT_EVENT_SUBSCRIBED, &event);
    return 17;
}

static void send_data(const char *path, const char *body, bool retain)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "%s%s", prefix, path);
    esp_mqtt_event_t ev = {.topic = topic, .topic_len = (int)strlen(topic), .data = (char *)body,
                           .retain = retain, .data_len = (int)strlen(body), .total_data_len = (int)strlen(body)};
    active->fn(active->ctx, NULL, MQTT_EVENT_DATA, &ev);
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t c, const char *topic, const char *data, int len, int qos, int retain)
{
    (void)c; (void)len;
    assert(!strncmp(topic,"R/a1b2c3d4e5f6/",15) && !qos && !retain);
    if (strstr(topic,"DeviceInstance")) {
        beats++;
        if (reply_heartbeat) send_data("system/0/DeviceInstance","{\"value\":0}",false);
    } else if (!strstr(data, "suppress-republish")) {
        captures++;
        assert(strstr(topic,"keepalive"));
        cJSON *req = cJSON_Parse(data);
        cJSON *opts = cJSON_GetObjectItem(req, "keepalive-options");
        const char *echo = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(opts,0),"full-publish-completed-echo"));
        assert(echo && strlen(echo) == 16);
        send_data("system/0/Dc/Pv/Power","{\"value\":1200}",retained_only);
        if (!retained_only) {
            send_data("system/0/Ac/PvOnGrid/Total/Power","{\"value\":800}",false);
            send_data("system/0/Ac/PvOnGrid/L1/Power","{\"value\":800}",false);
            send_data("system/0/Ac/PvOnOutput/Total/Power","{\"value\":100}",false);
            send_data("system/0/Ac/PvOnGenset/NumberOfPhases","{\"value\":2}",false);
            send_data("system/0/Ac/PvOnGenset/L1/Power","{\"value\":300}",false);
            if (!partial_pv) send_data("system/0/Ac/PvOnGenset/L2/Power","{\"value\":100}",false);
            send_data("system/0/Ac/Grid/NumberOfPhases","{\"value\":3}",false);
            send_data("system/0/Ac/Grid/L1/Power","{\"value\":300}",false);
            send_data("system/0/Ac/Grid/L2/Power","{\"value\":-500}",false);
            send_data("system/0/Ac/Grid/L3/Power","{\"value\":100}",false);
            send_data("system/0/Ac/Consumption/Total/Power","{\"value\":2000}",false);
            if (battery) {
                send_data("system/0/Dc/Battery/Power","{\"value\":250}",false);
                send_data("system/0/Dc/Battery/Soc","{\"value\":100}",false);
                send_data("system/0/Dc/Battery/Temperature","{\"value\":0}",false);
            }
        }
        char reply[96];
        snprintf(reply,sizeof(reply),"{\"value\":123,\"full-publish-completed-echo\":\"%s\"}",wrong_echo ? "another-client" : echo);
        send_data("full_publish_completed",reply,false);
        cJSON_Delete(req);
    }
    return 0;
}

int main(void)
{
    app_inverter_config_t cfg = {.host="gx.local",.profile="victron_mqtt",.device="A1B2C3D4E5F6"};
    app_energy_sample_t s;
    wrong_echo = true;
    assert(!app_inverter_victron_read(&cfg,&s));
    wrong_echo = false;
    assert(app_inverter_victron_read(&cfg,&s));
    assert(fabs(s.power_pv_kw - 2.5) < 1e-9 && fabs(s.power_grid_kw + 0.1) < 1e-9);
    assert(s.battery_unavailable && s.soc_unavailable && !s.battery_temperature_valid);
    assert(app_inverter_victron_read(&cfg,&s));
    assert(starts == 1 && subscriptions == 1 && captures == 2 && beats == 1);
    send_data("system/0/Ac/Grid/L2/Power","{\"value\":null}",false);
    assert(app_inverter_victron_read(&cfg,&s) && s.grid_unavailable);
    reply_heartbeat = false;
    assert(!app_inverter_victron_read(&cfg,&s));
    partial_pv = true;
    assert(!app_inverter_victron_read(&cfg,&s));
    app_inverter_victron_close();
    cfg.battery = battery = true;
    tls = true;
    cfg.port = 8883;
    cfg.trusted_pem = "trusted-pem";
    partial_pv = false;
    assert(app_inverter_victron_read(&cfg,&s) && !s.soc_unavailable && s.soc_bat_pct == 100);
    assert(fabs(s.power_bat_kw + 0.25) < 1e-9 && s.battery_temperature_valid && s.battery_temperature_c == 0);
    active->fn(active->ctx,NULL,MQTT_EVENT_DISCONNECTED,NULL);
    assert(!app_inverter_victron_read(&cfg,&s));
    retained_only = true;
    assert(!app_inverter_victron_read(&cfg,&s));
    /* Két elmaradt válasz bontja a TCP-n látszólag még élő munkamenetet. */
    wrong_echo = true;
    assert(!app_inverter_victron_read(&cfg,&s));
    assert(active && stops == 2);
    assert(!app_inverter_victron_read(&cfg,&s));
    assert(!active && stops == 3);
    wrong_echo = retained_only = false;
    assert(app_inverter_victron_read(&cfg,&s));
    app_inverter_victron_close();
    assert(starts == 4 && stops == 4);
    puts("PASS: GX MQTT feliratkozás, TLS, frissesség, részleges fázisok és munkamenetváltás");
}
