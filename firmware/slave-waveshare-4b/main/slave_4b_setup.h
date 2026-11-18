#pragma once

#include "app_config.h"

/* On success the saved configuration takes effect at the next boot. */
esp_err_t slave_4b_setup_run(app_config_t *config);
