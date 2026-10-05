/* Re-created transparent PNG artwork. Original sources: assets/source/.
 * Dimensions are normalized by tools/prepare_assets.py. Byte counts are
 * measured by CMake, so replacing an image cannot truncate its PNG stream.
 * ESP-IDF 5.5 names embedded symbols from the filename, without directories.
 */
#include "app_assets.h"
#include "app_asset_sizes.h"

#define PNG_IMAGE(name, embedded_symbol, byte_count) \
    extern const uint8_t _binary_##embedded_symbol##_start[]; \
    const lv_image_dsc_t name = { \
        .header = { \
            .magic = LV_IMAGE_HEADER_MAGIC, \
            .cf = LV_COLOR_FORMAT_RAW_ALPHA, \
            .w = 128, .h = 128, .stride = 0, \
        }, \
        .data_size = (byte_count), \
        .data = _binary_##embedded_symbol##_start, \
    }

PNG_IMAGE(app_img_header_home_leaf, header_home_leaf_png, APP_PNG_HEADER_HOME_LEAF_SIZE);
PNG_IMAGE(app_img_leaf, metric_leaf_png, APP_PNG_METRIC_LEAF_SIZE);
PNG_IMAGE(app_img_house, metric_house_png, APP_PNG_METRIC_HOUSE_SIZE);
PNG_IMAGE(app_img_grid, flow_grid_png, APP_PNG_FLOW_GRID_SIZE);
PNG_IMAGE(app_img_grid_metric, metric_grid_png, APP_PNG_METRIC_GRID_SIZE);
PNG_IMAGE(app_img_coins, metric_coins_png, APP_PNG_METRIC_COINS_SIZE);
PNG_IMAGE(app_img_co2, summary_co2_png, APP_PNG_SUMMARY_CO2_SIZE);
PNG_IMAGE(app_img_tree, summary_tree_png, APP_PNG_SUMMARY_TREE_SIZE);
PNG_IMAGE(app_img_bulb, summary_bulb_png, APP_PNG_SUMMARY_BULB_SIZE);
PNG_IMAGE(app_img_battery, flow_battery_png, APP_PNG_FLOW_BATTERY_SIZE);
PNG_IMAGE(app_img_solar, flow_solar_png, APP_PNG_FLOW_SOLAR_SIZE);
PNG_IMAGE(app_img_home_scene, flow_house_png, APP_PNG_FLOW_HOUSE_SIZE);
PNG_IMAGE(app_img_hub, flow_hub_png, APP_PNG_FLOW_HUB_SIZE);
PNG_IMAGE(app_img_weather_sun, weather_sun_png, APP_PNG_WEATHER_SUN_SIZE);
PNG_IMAGE(app_img_weather_cloud, weather_cloud_png, APP_PNG_WEATHER_CLOUD_SIZE);
PNG_IMAGE(app_img_weather_partly, weather_partly_png, APP_PNG_WEATHER_PARTLY_SIZE);
PNG_IMAGE(app_img_weather_rain, weather_rain_png, APP_PNG_WEATHER_RAIN_SIZE);
PNG_IMAGE(app_img_weather_snow, weather_snow_png, APP_PNG_WEATHER_SNOW_SIZE);
PNG_IMAGE(app_img_weather_moon, weather_moon_png, APP_PNG_WEATHER_MOON_SIZE);
PNG_IMAGE(app_img_wind, wind_png, APP_PNG_WIND_SIZE);
PNG_IMAGE(app_img_wifi_off, wifi_off_png, APP_PNG_WIFI_OFF_SIZE);
PNG_IMAGE(app_img_grid_off, grid_off_png, APP_PNG_GRID_OFF_SIZE);
PNG_IMAGE(app_img_status_normal, status_normal_png, APP_PNG_STATUS_NORMAL_SIZE);
PNG_IMAGE(app_img_status_prepare, status_prepare_png, APP_PNG_STATUS_PREPARE_SIZE);
PNG_IMAGE(app_img_status_island, status_island_png, APP_PNG_STATUS_ISLAND_SIZE);
PNG_IMAGE(app_img_status_error, status_error_png, APP_PNG_STATUS_ERROR_SIZE);
PNG_IMAGE(app_img_mqtt_off, mqtt_off_png, APP_PNG_MQTT_OFF_SIZE);
PNG_IMAGE(app_img_waiting_data, waiting_data_png, APP_PNG_WAITING_DATA_SIZE);
PNG_IMAGE(app_img_timeout, timeout_png, APP_PNG_TIMEOUT_SIZE);


PNG_IMAGE(app_img_grid_import, metric_grid_import_png, APP_PNG_METRIC_GRID_IMPORT_SIZE);
PNG_IMAGE(app_img_grid_export, metric_grid_export_png, APP_PNG_METRIC_GRID_EXPORT_SIZE);
