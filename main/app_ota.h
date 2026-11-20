#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include "app_config.h"

/* OTA próba-image ötperces automatikus validációjának elindítása. */
esp_err_t app_ota_start_validation_task(void);
/* Az első beállítás mentéskor újraindít. Csak a validáció befejezése után
 * indítható az ezt engedélyező webfelület. Elfogadott képen azonnal visszatér. */
void app_ota_wait_for_validation(void);

/* Indulás után egyszer: ha az előző OTA visszaállt, naplózza a verzióját és –
 * ha az alkalmazás még le tudta írni – a visszaállás okát is. A RAM-napló a
 * következő újraindításig megőrzi, így a Slave diagnosztikáján is látszik. */
void app_ota_report_boot_state(void);

/* Napváltásra ébresztett, opcionális GitHub OTA-feladat indítása. */
esp_err_t app_ota_start_auto_update(const app_config_t *cfg);

/* Kézi OTA, automatikus OTA és webes újraindítás nem futhat párhuzamosan. */
bool app_ota_try_begin_operation(void);
void app_ota_end_operation(void);
/* Igaz, amíg ilyen művelet fut: a kijelző ilyenkor statikus üzenetre vált. */
bool app_ota_operation_active(void);

/* Egy rövid életű, kimenő inverteres HTTPS-munkamenet fér el egyszerre.
 * Foglaltságkor a lekérdezés később próbálkozik, a webes mentés elérhető marad. */
bool app_ota_try_background_https(void);
void app_ota_end_background_https(void);

/* Ugyanazon GitHub-ellenőrzés azonnali, aszinkron indítása; az éjszakai
 * kapcsolót nem módosítja, kikapcsolt automatikánál is tesztelhető. */
esp_err_t app_ota_request_auto_update(void);

/* Egy opcionális, korlátos háttérmunka a meglévő OTA-vermen. Az azonos
 * várakozó run/argument pár összevonódik; a callback futása közbeni új
 * beadás megmarad. OTA kérés és aktív művelet mindig elsőbbséget kap.
 * False: a worker még nem indult, vagy másik callback vár a slotban.
 * A callback minden zár nélkül fut; az argumentum élettartamáért a hívó
 * felel. A callback az időközben indult OTA-t maga is ellenőrizze. */
bool app_ota_background_post(void (*run)(void *), void *argument);
