#pragma once

#include "app_loxone_shading.h"
#include "app_loxone_home_runtime.h"
#include "esp_err.h"

/* Kis, mutexelt pillanatkép a látható árnyékoláskártyához. A GUI nem olvas
 * katalógust, nem foglal blokkmodellt és nem vár hálózati válaszra. */
bool app_loxone_shading_summary(const char *card_id, unsigned revision,
                               lx_shading_summary_t *out);

/* A gombnyomás ugyanabban a kritikus szakaszban ellenőrzi a profilrevíziót,
 * amelyben sorba áll. Régi kártya nem célozhat frissen átpárosított blokkot. */
esp_err_t app_loxone_shading_command(const char *card_id, unsigned revision,
                                    lx_home_command_t action);

/* Az egyedi redőny felugró ablakának egyszeri, abszolút célértéke.
 * A régi profilból nyitva maradt ablak revíziója nem vezérelhet új kártyát. */
esp_err_t app_loxone_shading_position(const char *card_id, unsigned revision,
                                     unsigned percent);
