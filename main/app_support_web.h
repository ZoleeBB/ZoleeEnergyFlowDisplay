#pragma once

#include <stdbool.h>
#include <string.h>

#include "esp_http_server.h"
#include "app_support_links.h"

/* Közös támogatási blokk: nincs fizetési kérés, háttérkapcsolat vagy kijelzőállapot.
 * A kicsinyített logó is a flashben lévő HTML része, de nyelvtől független: egyetlen
 * példányban tároljuk, és a kártya két fele közé küldjük. A darabok közvetlenül a
 * flashből mennek, nem a sendf() verempufferén át; a PNG-t a böngésző dekódolja. */
static inline esp_err_t app_web_send_support(httpd_req_t *req, bool hu)
{
    const char *head = hu ? app_support_card_hu : app_support_card_en;
    const char *tail = hu ? app_support_card_hu_tail : app_support_card_en_tail;
    if (!head[0]) {
        return ESP_OK;
    }
    esp_err_t result = httpd_resp_send_chunk(req, head, strlen(head));
    if (result == ESP_OK && app_support_logo[0]) {
        result = httpd_resp_send_chunk(req, app_support_logo, strlen(app_support_logo));
    }
    if (result == ESP_OK && tail[0]) {
        result = httpd_resp_send_chunk(req, tail, strlen(tail));
    }
    return result;
}

/* Az oldalt másolás és új heapfoglalás nélkül, a jelölt helyen egészítjük ki.
 * Üres linkkonfigurációval az eredeti oldal marad elérhető; nincs hibás gomb.
 * Az első küldési hibánál leállunk, a sikeres chunked választ egyszer zárjuk. */
static inline esp_err_t app_web_send_page(httpd_req_t *req, const char *page, bool hu)
{
    static const char marker[] = "<!--APP_SUPPORT-->";
    const char *slot = strstr(page, marker);
    const char *card = hu ? app_support_card_hu : app_support_card_en;
    if (!slot || !card[0]) {
        return httpd_resp_sendstr(req, page);
    }
    esp_err_t result = ESP_OK;
    if (slot != page) {
        result = httpd_resp_send_chunk(req, page, slot - page);
    }
    if (result == ESP_OK) {
        result = app_web_send_support(req, hu);
    }
    const char *tail = slot + sizeof(marker) - 1;
    if (result == ESP_OK && tail[0]) {
        result = httpd_resp_send_chunk(req, tail, strlen(tail));
    }
    if (result == ESP_OK) {
        result = httpd_resp_send_chunk(req, NULL, 0);
    }
    return result;
}
