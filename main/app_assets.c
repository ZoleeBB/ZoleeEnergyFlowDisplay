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
SVG_IMAGE(app_img_co2,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M16 47h31c7 0 11-4 11-10 0-6-4-10-10-11-2-9-9-14-18-12-7 1-12 6-13 13-6 1-10 5-10 10 0 6 4 10 9 10Z' fill='#747F91'/>"
"<path d='M21 31c0-4 3-6 7-6 2 0 4 1 5 2l-2 3c-1-1-2-1-3-1-2 0-3 1-3 3s1 3 3 3c1 0 2 0 3-1l2 3c-1 1-3 2-5 2-4 0-7-3-7-8Zm14 1c0-4 3-7 7-7s7 3 7 7-3 7-7 7-7-3-7-7Zm4 0c0 2 1 3 3 3s3-1 3-3-1-3-3-3-3 1-3 3Z' fill='#FFFFFF'/>"
"<path d='M48 42h7l-1 2h-3l3 2v2h-7v-2h3l-3-2v-2Z' fill='#FFFFFF'/>"
"</svg>");

/* Fa-egyenérték. */
SVG_IMAGE(app_img_tree,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M30 52h5V34h-5z' fill='#70451F'/>"
"<circle cx='32' cy='21' r='14' fill='#27BE35'/>"
"<circle cx='22' cy='30' r='9' fill='#38CC43'/>"
"<circle cx='42' cy='31' r='9' fill='#22AD31'/>"
"<path d='M32 50V23M32 35l-8-7M32 39l9-8' fill='none' stroke='#145B20' stroke-width='3' stroke-linecap='round'/>"
"</svg>");

/* Önellátás / energiahatékonyság jelző. */
SVG_IMAGE(app_img_bulb,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M19 26c0-8 6-14 13-14s13 6 13 14c0 6-3 9-7 13-2 2-3 4-3 7h-6c0-3-1-5-3-7-4-4-7-7-7-13Z' fill='#FFB800'/>"
"<path d='M24 23c2-5 6-7 11-7' fill='none' stroke='#FFE27A' stroke-width='3' stroke-linecap='round'/>"
"<path d='M26 47h12v5H26z' fill='#69727B'/><path d='M28 55h8' stroke='#69727B' stroke-width='3' stroke-linecap='round'/>"
"<g stroke='#FFB300' stroke-width='2.5' stroke-linecap='round'><path d='M32 5v4M12 14l4 3M52 14l-4 3M8 29h5M51 29h5'/></g>"
"</svg>");

/* Zöld akkumulátor villámmal. */
PNG_IMAGE(app_img_battery, assets_png_flow_battery_png, 3388);

/* Kék napelem + nap. */
PNG_IMAGE(app_img_solar, assets_png_flow_solar_png, 4225);

/* Részletesebb ház a központi energiaábrához. */
PNG_IMAGE(app_img_home_scene, assets_png_flow_house_png, 3742);

/* Központi, szürke energiaelosztó villám. */
PNG_IMAGE(app_img_hub, assets_png_flow_hub_png, 2968);

/* Időjárás ikonok. */
SVG_IMAGE(app_img_weather_sun,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='32' cy='32' r='10' fill='#FFC400'/>"
"<g stroke='#FFB300' stroke-width='3' stroke-linecap='round'><path d='M32 8v8M32 48v8M8 32h8M48 32h8M15 15l6 6M43 43l6 6M49 15l-6 6M21 43l-6 6'/></g>"
"</svg>");

SVG_IMAGE(app_img_weather_cloud,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M14 47h35c7 0 11-4 11-10 0-7-5-11-11-11-2-8-8-13-16-13-9 0-16 6-17 15-7 0-12 4-12 10 0 5 5 9 10 9Z' fill='#93A1AE'/>"
"</svg>");

SVG_IMAGE(app_img_weather_partly,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<circle cx='19' cy='20' r='9' fill='#FFC400'/>"
"<g stroke='#FFB300' stroke-width='2.2' stroke-linecap='round'><path d='M19 5v6M19 29v6M4 20h6M28 20h6M9 10l4 4M29 10l-4 4'/></g>"
"<path d='M20 49h31c6 0 10-4 10-9 0-6-4-10-10-10-2-7-7-11-14-11-8 0-14 5-15 13-6 0-11 4-11 9 0 5 4 8 9 8Z' fill='#93A1AE'/>"
"</svg>");

SVG_IMAGE(app_img_weather_rain,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M13 38h38c6 0 10-4 10-9 0-6-5-10-11-10-2-7-8-11-15-11-8 0-14 5-15 13-7 0-12 4-12 9 0 5 5 8 5 8Z' fill='#8797A6'/>"
"<path d='M20 44l-4 9M33 44l-4 9M46 44l-4 9' stroke='#2D9CDB' stroke-width='3.4' stroke-linecap='round'/>"
"</svg>");

SVG_IMAGE(app_img_weather_snow,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M13 36h38c6 0 10-4 10-9 0-6-5-10-11-10-2-7-8-11-15-11-8 0-14 5-15 13-7 0-12 4-12 9 0 5 5 8 5 8Z' fill='#8797A6'/>"
"<g fill='#DDF4FF' stroke='#78BCE8'><circle cx='20' cy='47' r='3'/><circle cx='32' cy='52' r='3'/><circle cx='44' cy='47' r='3'/></g>"
"</svg>");

SVG_IMAGE(app_img_weather_moon,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<path d='M45 45C29 47 17 36 19 21c1-7 5-12 10-15-3 12 2 24 13 30 5 3 10 3 15 2-3 4-7 6-12 7Z' fill='#A9B9C7'/>"
"<circle cx='45' cy='15' r='2' fill='#DDE8F0'/><circle cx='51' cy='24' r='1.5' fill='#DDE8F0'/>"
"</svg>");

SVG_IMAGE(app_img_wind,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<g fill='none' stroke='#7B8794' stroke-width='3' stroke-linecap='round'>"
"<path d='M8 23h30c7 0 8-9 2-11-4-1-7 1-8 4'/><path d='M8 33h40c8 0 9 11 1 13-5 1-8-2-9-5'/><path d='M8 43h22'/>"
"</g></svg>");

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
SVG_IMAGE(app_img_wifi_off,
"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'>"
"<g fill='none' stroke-linecap='round'><path d='M10 25c13-12 31-12 44 0M17 33c9-8 21-8 30 0M25 41c4-4 10-4 14 0' stroke='#718096' stroke-width='4'/><circle cx='32' cy='49' r='3.5' fill='#718096'/><path d='M12 12 52 52' stroke='#E53935' stroke-width='5'/></g>"
"</svg>");

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
