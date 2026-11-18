#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Az SD CID stabil mezői. A felhasználó által jóváhagyott kártyát akkor is
 * felismerjük, ha az újraindítás vagy egy megszakítás alatt kicserélik. */
typedef struct {
    uint32_t serial;
    uint16_t oem, date;
    uint8_t manufacturer, revision;
    uint8_t product[5], reserved;
} app_reset_card_t;

enum { APP_RESET_INTERNAL = 1, APP_RESET_WITH_CARD = 2 };

typedef struct {
    uint8_t version, scope, completed, reserved;
    app_reset_card_t card;
} app_reset_plan_t;

/* Sikeres kérésből csak tartós szándék és újraindítás következik. A tényleges
 * törléshez az alkalmazás összes élő írójának le kell állnia. */
esp_err_t app_factory_reset_request(bool require_sd);
esp_err_t app_factory_reset_read(app_reset_plan_t *plan);
typedef enum { APP_RESET_JOURNAL, APP_RESET_CARD, APP_RESET_RADIO, APP_RESET_STORAGE, APP_RESET_RESTART } app_reset_stage_t;
esp_err_t app_factory_reset_apply(app_reset_plan_t *plan, const char *sd_root, app_reset_stage_t *stage);
esp_err_t app_factory_reset_remember_sd(void);

/* A 10.1-es Master tárkezelője olvassa a csatolt, válaszkész kártyát. A
 * csatolás újrapróbálása csak a boot során, a többi SD-feladat előtt futhat. */
esp_err_t app_factory_reset_card(app_reset_card_t *card);
esp_err_t app_factory_reset_remount(void);
