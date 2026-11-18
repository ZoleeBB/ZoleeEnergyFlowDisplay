#include "slave_4b_setup.h"
#include "app_boot_config.h"
#include "app_setup_ui.h"
#include "app_setup_wifi.h"
#include "app_time.h"
#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    app_config_t *config;
    char token[17];
    atomic_bool saved;
} setup_context_t;

static void setup_render(lv_timer_t *timer)
{
    (void)timer;
    app_setup_wifi_state_t state;
    app_setup_wifi_snapshot(&state);
    app_setup_ui_update(lv_screen_active(), 720, 720, &state);
}

static esp_err_t setup_page(httpd_req_t *req)
{
    setup_context_t *ctx = req->user_ctx;
    static const char introduction[] =
        "<!doctype html><html lang=hu><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>4B kijelző beállítása</title><style>body{max-width:35rem;margin:2rem auto;padding:1rem;font:18px system-ui}"
        "select,button{font:inherit;padding:.5rem}</style><h1>4B kijelző beállítása</h1>"
        "<p>A 4B a Master kijelzőtől kapja az adatokat Bluetooth-on. A mentés után nyisd meg "
        "az új párosítási ablakot a Master webfelületén, és a 4B érintőképernyőjén add meg a Master PIN-kódját.</p>"
        "<p>A párosított kijelző nevét, fényerejét, tájolását és az inverteres időprogramját a Master "
        "Slave kijelzők beállításainál módosíthatod.</p><form method=post action='/setup'>"
        "<label>Üzemmód <select required name='mode'><option value=''>Válassz…</option>"
        "<option value=slave>Slave kijelző</option></select></label>";
    char ending[192];
    snprintf(ending, sizeof(ending),
             "<input type=hidden name=token value='%s'><p><button>Mentés és újraindítás</button></p></form></html>",
             ctx->token);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (httpd_resp_sendstr_chunk(req, introduction) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, ending) != ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t save_slave(const app_config_t *old)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *wifi = root ? cJSON_AddObjectToObject(root, "wifi") : NULL;
    char *json = NULL;
    if (wifi && cJSON_AddStringToObject(root, "DataReceptMode", "slave") &&
        cJSON_AddStringToObject(wifi, "ssid", old->wifi.ssid) &&
        cJSON_AddStringToObject(wifi, "password", old->wifi.password))
        json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return ESP_ERR_NO_MEM;
    app_config_t *next = malloc(sizeof(*next));
    char error[128];
    esp_err_t result = next ? app_boot_config_complete(json, strlen(json), next, error, sizeof(error)) : ESP_ERR_NO_MEM;
    free(next);
    cJSON_free(json);
    return result;
}

static esp_err_t setup_save(httpd_req_t *req)
{
    setup_context_t *ctx = req->user_ctx;
    char body[64], mode[10], token[sizeof(ctx->token)];
    if (!req->content_len || req->content_len >= sizeof(body))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Hibás kérés.");
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) return ESP_FAIL;
        received += (size_t)n;
    }
    body[received] = 0;
    if (httpd_query_key_value(body, "mode", mode, sizeof(mode)) != ESP_OK || strcmp(mode, "slave") ||
        httpd_query_key_value(body, "token", token, sizeof(token)) != ESP_OK || strcmp(token, ctx->token))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Töltsd újra az oldalt és válassz üzemmódot.");
    if (!atomic_load(&ctx->saved) && save_slave(ctx->config) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "A mentés sikertelen. Próbáld újra.");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t sent = httpd_resp_sendstr(req, "A Slave módot elmentettük. A kijelző újraindul. Engedélyezd a párosítást a Masteren.");
    atomic_store(&ctx->saved, true);
    return sent;
}

esp_err_t slave_4b_setup_run(app_config_t *cfg)
{
    if (!bsp_display_lock(5000)) return ESP_ERR_TIMEOUT;
    lv_timer_t *timer = lv_timer_create(setup_render, 200, NULL);
    bsp_display_unlock();
    if (!timer) return ESP_ERR_NO_MEM;
    esp_err_t err = app_setup_wifi_provision(cfg);
    if (err != ESP_OK) return err;
    if ((err = app_boot_config_save_setup_wifi(cfg)) != ESP_OK) return err;
    app_setup_wifi_set_phase(APP_SETUP_WAIT_TIME);
    /* The first boot uses network time. On the next boot the confirmed
     * Slave role switches to the authenticated Master's clock. */
    if ((err = app_time_init(cfg)) != ESP_OK) return err;
    while (app_time_sync_once() != ESP_OK) vTaskDelay(pdMS_TO_TICKS(1000));
    httpd_config_t settings = HTTPD_DEFAULT_CONFIG();
    settings.stack_size = 8192;
    httpd_handle_t server = NULL;
    if ((err = httpd_start(&server, &settings)) != ESP_OK) return err;
    setup_context_t ctx = {.config = cfg};
    snprintf(ctx.token, sizeof(ctx.token), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = setup_page, .user_ctx = &ctx};
    httpd_uri_t post = {.uri = "/setup", .method = HTTP_POST, .handler = setup_save, .user_ctx = &ctx};
    if ((err = httpd_register_uri_handler(server, &get)) == ESP_OK &&
        (err = httpd_register_uri_handler(server, &post)) == ESP_OK) {
        app_setup_wifi_set_phase(APP_SETUP_WEB);
        while (!atomic_load(&ctx.saved)) vTaskDelay(pdMS_TO_TICKS(200));
    }
    httpd_stop(server);
    if (err == ESP_OK) esp_restart();
    return err;
}
