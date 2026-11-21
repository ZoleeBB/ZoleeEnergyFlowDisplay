#pragma once

#include "esp_err.h"

/* Csak induláskor, az SD csatolása után, minden visszaállítás és háttéríró
 * előtt. sd_root == NULL: az eszköz igazoltan kártya nélküli. Hiba után a
 * hívó megtartja a tartós kérésjelzőt, és a teljes műveletet megismételheti. */
esp_err_t app_factory_reset_storage(const char *sd_root);
