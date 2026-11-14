#pragma once
/* A kötelező PNG-teszt nem igényel letöltött LVGL-t. A valódi JPEG-dekóder
 * külön ugyanebben a tesztben fut, ha LVGL_SOURCE_DIR rendelkezésre áll. */
#define LV_USE_TJPGD 0
