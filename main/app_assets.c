/*
 * A dashboard végleges grafikai assetjei.
 *
 * Az SVG-k 64x64-es, átlátszó hátterű vektorok. LVGL 9.5 SVG dekóderrel
 * közvetlenül a firmware-ből rendereli őket, ezért nincs szükség futás közbeni
 * primitív ikonrajzolásra és a felbontásfüggetlen megjelenés is megmarad.
 */
#include "app_assets.h"

/*
 * A látványtervből készített, finom részletességű PNG-k közvetlenül a
 * firmware image-be vannak ágyazva. Az LVGL LodePNG dekódere futás közben
 * RGBA képpé alakítja őket. A fájlok 128×128-as, átlátszó hátterű assetek.
 */
#define PNG_IMAGE(name, embedded_symbol, byte_count) \
    extern const uint8_t _binary_##embedded_symbol##_start[]; \
    const lv_image_dsc_t name = { \
        .header = { \
            .magic = LV_IMAGE_HEADER_MAGIC, \
            .cf = LV_COLOR_FORMAT_RAW_ALPHA, \
            .w = 128, \
            .h = 128, \
            .stride = 0, \
        }, \
        .data_size = (byte_count), \
        .data = _binary_##embedded_symbol##_start, \
    }


#define SVG_IMAGE(name, svg_text) \
    static const uint8_t name##_data[] = svg_text; \
    const lv_image_dsc_t name = { \
        .header = { \
            .magic = LV_IMAGE_HEADER_MAGIC, \
            .cf = LV_COLOR_FORMAT_RAW, \
            .w = 64, \
            .h = 64, \
            .stride = 0, \
        }, \
        .data_size = sizeof(name##_data) - 1, \
        .data = name##_data, \
    }

/* Fejléc: ház + levél, ugyanazzal az otthoni/öko karakterrel, mint a mockupon. */
PNG_IMAGE(app_img_header_home_leaf, assets_png_header_home_leaf_png, 1717);

/* Zöld levél: termelés / megújuló energia. */
PNG_IMAGE(app_img_leaf, assets_png_metric_leaf_png, 3114);

/* Narancs lakóház: fogyasztás. */
PNG_IMAGE(app_img_house, assets_png_metric_house_png, 3155);

/* Kék villamos hálózati oszlop. */
PNG_IMAGE(app_img_grid, assets_png_flow_grid_png, 4762);

/* A bal oldali napi hálózati kártya világoskék, mockup-azonos ikonja. */
PNG_IMAGE(app_img_grid_metric, assets_png_metric_grid_png, 3657);

/* Leválasztott hálózat szigetüzemhez. */
SVG_IMAGE(app_img_grid_off,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<g opacity='.7'>"
"<path d='M31 8 19 55h5l3-13h10l3 13h5L33 8h-2Z' fill='#E4EBF0' stroke='#62788C' stroke-width='2.5'/>"
"<path d='M24 18h16M22 27h20M20 36h24M13 23h38M25 17l15 10-18 9 16 8M39 17 24 27l18 9-16 8' fill='none' stroke='#71869A' stroke-width='2' stroke-linecap='round'/>"
"</g>"
"<path d='M12 52 52 12' stroke='#2F80ED' stroke-width='5' stroke-linecap='round'/>"
"</svg>");

/* Arany érmek: pénzügyi megtakarítás. */
PNG_IMAGE(app_img_coins, assets_png_metric_coins_png, 3072);

/* CO2 felhő: környezeti megtakarítás. */
PNG_IMAGE(app_img_co2, assets_png_summary_co2_png, 3214);

/* Fa-egyenérték. */
PNG_IMAGE(app_img_tree, assets_png_summary_tree_png, 2918);

/* Önellátás / energiahatékonyság jelző. */
PNG_IMAGE(app_img_bulb, assets_png_summary_bulb_png, 2489);

/* Zöld akkumulátor villámmal. */
PNG_IMAGE(app_img_battery, assets_png_flow_battery_png, 3388);

/* Kék napelem + nap. */
PNG_IMAGE(app_img_solar, assets_png_flow_solar_png, 4225);

/* Részletesebb ház a központi energiaábrához. */
PNG_IMAGE(app_img_home_scene, assets_png_flow_house_png, 3742);

/* Központi, szürke energiaelosztó villám. */
PNG_IMAGE(app_img_hub, assets_png_flow_hub_png, 2968);

/* Időjárás ikonok. */
PNG_IMAGE(app_img_weather_sun, assets_png_weather_sun_png, 954);

PNG_IMAGE(app_img_weather_cloud, assets_png_weather_cloud_png, 1483);

PNG_IMAGE(app_img_weather_partly, assets_png_weather_partly_png, 2072);

PNG_IMAGE(app_img_weather_rain, assets_png_weather_rain_png, 1683);

PNG_IMAGE(app_img_weather_snow, assets_png_weather_snow_png, 1981);

PNG_IMAGE(app_img_weather_moon, assets_png_weather_moon_png, 1302);

PNG_IMAGE(app_img_wind, assets_png_wind_png, 1335);

/* Rendszerállapot ikonok. */
SVG_IMAGE(app_img_status_normal,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='32' cy='32' r='23' fill='#DDF8E2'/>"
"<path d='M20 31 32 20l12 11-2 3-10-8-10 8-2-3Z' fill='#168B2B'/><path d='M23 31h18v14H23z' fill='#23B33A'/><path d='M30 36h5v9h-5z' fill='#EAFBEF'/>"
"<path d='m19 18 4 4 8-9' fill='none' stroke='#1CB33A' stroke-width='3' stroke-linecap='round' stroke-linejoin='round'/>"
"</svg>");

SVG_IMAGE(app_img_status_prepare,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='32' cy='32' r='23' fill='#FFF1DD'/><rect x='19' y='20' width='26' height='30' rx='5' fill='#F59E0B'/><rect x='26' y='15' width='12' height='5' rx='2' fill='#705B3B'/>"
"<path d='m35 24-8 12h6l-3 9 10-14h-6l1-7Z' fill='#FFFFFF'/><path d='M48 19v9h-9M48 28a15 15 0 0 0-8-13' fill='none' stroke='#F97316' stroke-width='2.5' stroke-linecap='round'/>"
"</svg>");

SVG_IMAGE(app_img_status_island,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='32' cy='32' r='23' fill='#E5F1FF'/><path d='M13 34 27 22l14 12-2 3-12-10-12 10-2-3Z' fill='#2274C9'/><path d='M17 34h20v14H17z' fill='#4A9BE8'/><path d='M24 39h6v9h-6z' fill='#EAF5FF'/>"
"<rect x='39' y='32' width='13' height='17' rx='3' fill='#2F7DCB'/><path d='m46 35-5 7h4l-2 5 6-8h-4l1-4Z' fill='#FFFFFF'/><path d='M45 17 55 27M55 17 45 27' stroke='#2676C5' stroke-width='3' stroke-linecap='round'/>"
"</svg>");

SVG_IMAGE(app_img_status_error,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='32' cy='32' r='23' fill='#FFE2E0'/><path d='M32 14 53 50H11L32 14Z' fill='#E53935'/><rect x='29.5' y='25' width='5' height='14' rx='2.5' fill='#FFFFFF'/><circle cx='32' cy='44' r='3' fill='#FFFFFF'/>"
"</svg>");

/* Kapcsolati hibaállapotok. */
PNG_IMAGE(app_img_wifi_off, assets_png_wifi_off_png, 2624);

SVG_IMAGE(app_img_mqtt_off,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M16 43h30c7 0 11-4 11-10 0-6-4-10-10-11-2-8-8-13-16-12-8 1-13 6-14 13-7 0-11 4-11 10 0 6 4 10 10 10Z' fill='#7B8B98'/>"
"<path d='M21 31h18M21 36h13' stroke='#FFFFFF' stroke-width='3' stroke-linecap='round'/><path d='M42 18 55 31M55 18 42 31' stroke='#E53935' stroke-width='4' stroke-linecap='round'/>"
"</svg>");

SVG_IMAGE(app_img_waiting_data,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<g fill='none' stroke='#FF9F1C' stroke-width='4' stroke-linecap='round' stroke-linejoin='round'><path d='M18 20h28l-7-7M46 20l-7 7'/><path d='M46 44H18l7 7M18 44l7-7'/></g><circle cx='32' cy='32' r='5' fill='#FFB340'/>"
"</svg>");

SVG_IMAGE(app_img_timeout,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='32' cy='32' r='22' fill='#FFF0DD'/><circle cx='32' cy='32' r='15' fill='none' stroke='#F59E0B' stroke-width='4'/><path d='M32 22v11l8 5' fill='none' stroke='#F59E0B' stroke-width='4' stroke-linecap='round'/><path d='M48 48l8 8' stroke='#E53935' stroke-width='5' stroke-linecap='round'/>"
"</svg>");
