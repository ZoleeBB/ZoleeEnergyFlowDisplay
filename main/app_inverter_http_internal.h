#pragma once

#include "app_inverter_driver.h"
#include "app_inverter_net.h"
#include <time.h>

typedef const char *inv_http_header_t[2];

/* A visszaadott cím/form-érték tulajdonosa a hívó. Csak a tényleges olvasás alatt él. */
char *inv_http_escape(const char *value);
cJSON *inv_http_fetch(const app_inverter_config_t *cfg, const char *domain, const char *path,
                      unsigned default_port, bool tls, const char *body,
                      const inv_http_header_t *headers, size_t count, char *token, size_t token_size);
int64_t inv_http_timestamp(const char *text, bool allow_site_local);
bool inv_http_number(const cJSON *node, double *value);
bool inv_http_scale(const cJSON *obj, const char *key, double scale, double *value);
bool inv_http_soc(const cJSON *node, app_energy_sample_t *sample);
void inv_http_home_from_ac(app_energy_sample_t *sample, double ac_kw);
/* Az aktív adatolvasás és a felderítő ugyanazt az MPPT-mérést értelmezi. */
bool inv_http_solplanet_matches(const cJSON *inverter, const char *serial);
bool inv_http_solplanet_dc(const cJSON *inverter, double *kw);
bool inv_http_local(const app_inverter_config_t *cfg, app_energy_sample_t *sample);
bool inv_http_cloud(const app_inverter_config_t *cfg, app_energy_sample_t *sample, int64_t *epoch);
void app_inverter_http_reset(void);
