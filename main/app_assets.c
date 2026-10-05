/* Re-created transparent PNG artwork. Original sources: assets/source/.
 * Dimensions are normalized by tools/prepare_assets.py. Byte counts are
 * measured by CMake, so replacing an image cannot truncate its PNG stream.
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

PNG_IMAGE(app_img_header_home_leaf, assets_png_header_home_leaf_png, APP_PNG_HEADER_HOME_LEAF_SIZE);
PNG_IMAGE(app_img_leaf, assets_png_metric_leaf_png, APP_PNG_METRIC_LEAF_SIZE);
PNG_IMAGE(app_img_house, assets_png_metric_house_png, APP_PNG_METRIC_HOUSE_SIZE);
PNG_IMAGE(app_img_grid, assets_png_flow_grid_png, APP_PNG_FLOW_GRID_SIZE);
PNG_IMAGE(app_img_grid_metric, assets_png_metric_grid_png, APP_PNG_METRIC_GRID_SIZE);
PNG_IMAGE(app_img_coins, assets_png_metric_coins_png, APP_PNG_METRIC_COINS_SIZE);
PNG_IMAGE(app_img_co2, assets_png_summary_co2_png, APP_PNG_SUMMARY_CO2_SIZE);
PNG_IMAGE(app_img_tree, assets_png_summary_tree_png, APP_PNG_SUMMARY_TREE_SIZE);
PNG_IMAGE(app_img_bulb, assets_png_summary_bulb_png, APP_PNG_SUMMARY_BULB_SIZE);
PNG_IMAGE(app_img_battery, assets_png_flow_battery_png, APP_PNG_FLOW_BATTERY_SIZE);
PNG_IMAGE(app_img_solar, assets_png_flow_solar_png, APP_PNG_FLOW_SOLAR_SIZE);
PNG_IMAGE(app_img_home_scene, assets_png_flow_house_png, APP_PNG_FLOW_HOUSE_SIZE);
PNG_IMAGE(app_img_hub, assets_png_flow_hub_png, APP_PNG_FLOW_HUB_SIZE);
PNG_IMAGE(app_img_weather_sun, assets_png_weather_sun_png, APP_PNG_WEATHER_SUN_SIZE);
PNG_IMAGE(app_img_weather_cloud, assets_png_weather_cloud_png, APP_PNG_WEATHER_CLOUD_SIZE);
PNG_IMAGE(app_img_weather_partly, assets_png_weather_partly_png, APP_PNG_WEATHER_PARTLY_SIZE);
PNG_IMAGE(app_img_weather_rain, assets_png_weather_rain_png, APP_PNG_WEATHER_RAIN_SIZE);
PNG_IMAGE(app_img_weather_snow, assets_png_weather_snow_png, APP_PNG_WEATHER_SNOW_SIZE);
PNG_IMAGE(app_img_weather_moon, assets_png_weather_moon_png, APP_PNG_WEATHER_MOON_SIZE);
PNG_IMAGE(app_img_wind, assets_png_wind_png, APP_PNG_WIND_SIZE);
PNG_IMAGE(app_img_wifi_off, assets_png_wifi_off_png, APP_PNG_WIFI_OFF_SIZE);
PNG_IMAGE(app_img_grid_off, assets_png_grid_off_png, APP_PNG_GRID_OFF_SIZE);
PNG_IMAGE(app_img_status_normal, assets_png_status_normal_png, APP_PNG_STATUS_NORMAL_SIZE);
PNG_IMAGE(app_img_status_prepare, assets_png_status_prepare_png, APP_PNG_STATUS_PREPARE_SIZE);
PNG_IMAGE(app_img_status_island, assets_png_status_island_png, APP_PNG_STATUS_ISLAND_SIZE);
PNG_IMAGE(app_img_status_error, assets_png_status_error_png, APP_PNG_STATUS_ERROR_SIZE);
PNG_IMAGE(app_img_mqtt_off, assets_png_mqtt_off_png, APP_PNG_MQTT_OFF_SIZE);
PNG_IMAGE(app_img_waiting_data, assets_png_waiting_data_png, APP_PNG_WAITING_DATA_SIZE);
PNG_IMAGE(app_img_timeout, assets_png_timeout_png, APP_PNG_TIMEOUT_SIZE);
