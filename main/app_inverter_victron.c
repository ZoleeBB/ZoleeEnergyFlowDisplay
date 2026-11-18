/* A helyi GX MQTT-kliens az aktuális inverterprofil tulajdona. A többi
 * adatforrás nem foglal hozzá puffert, szálat vagy TLS-kontextust. */
#include "app_inverter_driver.h"
#include "app_inverter_victron_model.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "mqtt_client.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GX_SUBSCRIBED BIT0
#define GX_REPLY BIT1
#define GX_LOST BIT2

typedef struct {
    esp_mqtt_client_handle_t mqtt;
    EventGroupHandle_t events;
    SemaphoreHandle_t lock;
    char id[13], echo[20];
    char *pem;
    bool battery, ready, started;
    uint32_t session;
    int64_t last_keepalive;
    inv_gx_model_t model;
} gx_client_t;

static gx_client_t *s_gx;

static void subscribe(gx_client_t *c)
{
    /* Csak a rendszer összegzett energiatopicjai érdekelnek. Akku nélküli
     * profil a brokerben sem iratkozik fel a Battery alatti értékekre. */
    static const char *const suffix[] = {"full_publish_completed", "system/0/DeviceInstance",
        "system/0/Ac/+/+/Power", "system/0/Ac/+/NumberOfPhases", "system/0/Dc/Pv/Power",
        "system/0/Dc/Battery/Power", "system/0/Dc/Battery/Soc", "system/0/Dc/Battery/Temperature"};
    char topics[8][66];
    esp_mqtt_topic_t filters[8];
    int n = c->battery ? 8 : 5;
    for (int i = 0; i < n; i++) {
        snprintf(topics[i], sizeof(topics[i]), "N/%s/%s", c->id, suffix[i]);
        filters[i].filter = topics[i];
        filters[i].qos = 0;
    }
    if (esp_mqtt_client_subscribe_multiple(c->mqtt, filters, n) < 0)
        xEventGroupSetBits(c->events, GX_LOST);
}

static void notify(gx_client_t *c, esp_mqtt_event_handle_t ev)
{
    /* A retain a régi broker tárolt értéke lehet. A mérési JSON skalár, a
     * több részletben jövő vagy túl nagy üzenet nem válhat friss adattá. */
    if (ev->retain || !ev->topic || ev->topic_len < 16 || ev->topic_len >= 90) return;
    char topic[90];
    memcpy(topic, ev->topic, ev->topic_len);
    topic[ev->topic_len] = '\0';
    if (topic[0] != 'N' || topic[1] != '/' || strncmp(topic + 2, c->id, 12) || topic[14] != '/') return;
    const char *path = topic + 15;
    cJSON *json = NULL;
    if (ev->data && ev->data_len > 0 && ev->data_len <= 240 &&
        ev->total_data_len == ev->data_len && ev->current_data_offset == 0) {
        char payload[241];
        memcpy(payload, ev->data, ev->data_len);
        payload[ev->data_len] = '\0';
        /* A kiválasztott D-Bus mezők skalárok; rekurzív JSON a hálózati
         * feladat kis vermére nem engedhető rá. */
        const char *object = strchr(payload, '{');
        if (object && !strchr(object + 1, '{') && !strchr(payload, '[') &&
            !memchr(payload, 0, (size_t)ev->data_len))
            json = cJSON_ParseWithOpts(payload, NULL, true);
    }
    cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "value");
    bool trigger = false;
    xSemaphoreTake(c->lock, portMAX_DELAY);
    if (!strcmp(path, "full_publish_completed")) {
        const char *echo = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(json, "full-publish-completed-echo"));
        if (echo && c->echo[0] && !strcmp(echo, c->echo) && cJSON_IsNumber(v)) {
            c->ready = true;
            c->echo[0] = 0;
            trigger = true;
        }
    } else if (!strncmp(path, "system/0/", 9)) {
        path += 9;
        if (!strcmp(path, "DeviceInstance")) {
            trigger = c->ready && cJSON_IsNumber(v) && v->valuedouble == 0;
            if (!cJSON_IsNumber(v)) c->ready = false;
        } else {
            inv_gx_update(&c->model, path, cJSON_IsNumber(v), cJSON_IsNumber(v) ? v->valuedouble : 0);
        }
    }
    xSemaphoreGive(c->lock);
    if (trigger) xEventGroupSetBits(c->events, GX_REPLY);
    cJSON_Delete(json);
}

static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)base;
    gx_client_t *c = arg;
    esp_mqtt_event_handle_t ev = data;
    switch (id) {
    case MQTT_EVENT_CONNECTED:
    case MQTT_EVENT_DISCONNECTED:
        xSemaphoreTake(c->lock, portMAX_DELAY);
        c->session++;
        c->ready = false;
        c->echo[0] = 0;
        memset(&c->model, 0, sizeof(c->model));
        xSemaphoreGive(c->lock);
        xEventGroupClearBits(c->events, GX_SUBSCRIBED | GX_REPLY);
        if (id == MQTT_EVENT_CONNECTED) {
            xEventGroupClearBits(c->events, GX_LOST);
            subscribe(c);
        } else {
            xEventGroupSetBits(c->events, GX_LOST);
        }
        break;
    case MQTT_EVENT_SUBSCRIBED:
        for (int i = 0; ev->data && i < ev->data_len; i++)
            if ((unsigned char)ev->data[i] >= 0x80) {
                xEventGroupSetBits(c->events, GX_LOST);
                return;
            }
        xEventGroupSetBits(c->events, GX_SUBSCRIBED);
        break;
    case MQTT_EVENT_DATA:
        notify(c, ev);
        break;
    default:
        break;
    }
}

void app_inverter_victron_close(void)
{
    gx_client_t *c = s_gx;
    if (!c) return;
    if (c->mqtt) {
        if (c->started) esp_mqtt_client_stop(c->mqtt);
        esp_mqtt_client_destroy(c->mqtt);
    }
    if (c->lock) vSemaphoreDelete(c->lock);
    if (c->events) vEventGroupDelete(c->events);
    free(c->pem);
    free(c);
    s_gx = NULL;
}

static bool start(const app_inverter_config_t *cfg)
{
    if (strlen(cfg->device) != 12 || strspn(cfg->device, "0123456789abcdefABCDEF") != 12) return false;
    gx_client_t *c = calloc(1, sizeof(*c));
    if (!c) return false;
    s_gx = c;
    for (int i = 0; i < 12; i++) c->id[i] = (char)tolower((unsigned char)cfg->device[i]);
    c->battery = cfg->battery;
    c->events = xEventGroupCreate();
    c->lock = xSemaphoreCreateMutex();
    bool has_pem = cfg->trusted_pem && cfg->trusted_pem[0];
    if (has_pem) c->pem = strdup(cfg->trusted_pem);
    if (!c->lock || !c->events || (has_pem && !c->pem)) goto failure;
    bool tls = cfg->port == 8883 || has_pem;
    esp_mqtt_client_config_t mqtt = {
        .broker.address.hostname=cfg->host, .broker.address.port=cfg->port ? cfg->port : tls ? 8883 : 1883,
        .broker.address.transport=tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP,
        .credentials.username=cfg->user[0] ? cfg->user : NULL,
        .credentials.authentication.password=cfg->secret[0] ? cfg->secret : NULL,
        .broker.verification.certificate=c->pem,
        .broker.verification.crt_bundle_attach=tls && !c->pem ? esp_crt_bundle_attach : NULL,
        .network.timeout_ms=1800, .network.reconnect_timeout_ms=5000,
        .task.stack_size=6144, .buffer.size=1024, .buffer.out_size=1024
    };
    c->mqtt = esp_mqtt_client_init(&mqtt);
    if (!c->mqtt || esp_mqtt_client_register_event(c->mqtt, ESP_EVENT_ANY_ID, event, c) != ESP_OK ||
        esp_mqtt_client_start(c->mqtt) != ESP_OK) goto failure;
    c->started = true;
    return true;
failure:
    app_inverter_victron_close();
    return false;
}

static bool publish(gx_client_t *c, const char *path, const char *body)
{
    char topic[80];
    snprintf(topic, sizeof(topic), "R/%s/%s", c->id, path);
    return esp_mqtt_client_publish(c->mqtt, topic, body, 0, 0, 0) >= 0;
}

bool app_inverter_victron_read(const app_inverter_config_t *cfg, app_energy_sample_t *out)
{
    if (!cfg || !out || strcmp(cfg->profile, "victron_mqtt")) return false;
    if (!s_gx && !start(cfg)) return false;
    gx_client_t *c = s_gx;
    EventBits_t bits = xEventGroupWaitBits(c->events, GX_SUBSCRIBED | GX_LOST, pdFALSE,
                                          pdFALSE, pdMS_TO_TICKS(1500));
    if (bits & GX_LOST) {
        app_inverter_victron_close();
        return false;
    }
    if (!(bits & GX_SUBSCRIBED)) return false;
    int64_t now = esp_timer_get_time();
    xEventGroupClearBits(c->events, GX_REPLY);
    xSemaphoreTake(c->lock, portMAX_DELAY);
    uint32_t session = c->session;
    bool first = !c->ready;
    char request[96];
    if (first) {
        snprintf(c->echo, sizeof(c->echo), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
        snprintf(request, sizeof(request), "{\"keepalive-options\":[{\"full-publish-completed-echo\":\"%s\"}]}", c->echo);
        memset(&c->model, 0, sizeof(c->model));
    }
    xSemaphoreGive(c->lock);
    if (first) {
        if (!publish(c, "keepalive", request)) return false;
        c->last_keepalive = now;
    } else {
        /* Az első teljes kép után a broker minden változást és érvénytelenítést
         * elküld. Az ötmásodperces célzott olvasás a D-Bus szolgáltatás élőségét
         * igazolja; nem árasztjuk el a többi klienst teljes GX újraközlésekkel. */
        if (now - c->last_keepalive >= 25000000) {
            if (!publish(c, "keepalive", "{\"keepalive-options\":[\"suppress-republish\"]}")) return false;
            c->last_keepalive = now;
        }
        if (!publish(c, "system/0/DeviceInstance", "")) return false;
    }
    bits = xEventGroupWaitBits(c->events, GX_REPLY | GX_LOST, pdFALSE, pdFALSE, pdMS_TO_TICKS(1500));
    xSemaphoreTake(c->lock, portMAX_DELAY);
    bool valid = (bits & GX_REPLY) && !(bits & GX_LOST) && c->session == session &&
                 c->ready && inv_gx_sample(&c->model, c->battery, out);
    if (!(bits & GX_REPLY)) c->ready = false;
    xSemaphoreGive(c->lock);
    return valid;
}
