#pragma once

#include <stdbool.h>

/* A jóváhagyott Master fogyasztói oldal geometriája (1280×800).
 * A hiányos utolsó sor középre kerül, a teljes sorok helye és a kártyaméret
 * az elemszámtól független. A rajzoló ezt használja, nem külön másolt képletet. */
#define LX_HOME_SCREEN_WIDTH 1280U
#define LX_HOME_SCREEN_HEIGHT 800U
#define LX_HOME_GRID_COLUMNS 4U
#define LX_HOME_GRID_MAX 12U
#define LX_HOME_CARD_WIDTH 292U
#define LX_HOME_CARD_HEIGHT 200U
#define LX_HOME_CARD_GAP 16U
#define LX_HOME_GRID_TOP 120U

typedef struct
{
    unsigned x, y, width, height;
} lx_home_card_rect_t;

static inline bool lx_home_card_rect(unsigned count, unsigned index, lx_home_card_rect_t *out)
{
    if (!out || count == 0 || count > LX_HOME_GRID_MAX || index >= count)
        return false;
    unsigned row = index / LX_HOME_GRID_COLUMNS;
    unsigned first = row * LX_HOME_GRID_COLUMNS;
    unsigned in_row = count - first;
    if (in_row > LX_HOME_GRID_COLUMNS)
        in_row = LX_HOME_GRID_COLUMNS;
    unsigned row_width = in_row * LX_HOME_CARD_WIDTH + (in_row - 1) * LX_HOME_CARD_GAP;
    out->x = (LX_HOME_SCREEN_WIDTH - row_width) / 2 +
             (index - first) * (LX_HOME_CARD_WIDTH + LX_HOME_CARD_GAP);
    out->y = LX_HOME_GRID_TOP + row * (LX_HOME_CARD_HEIGHT + LX_HOME_CARD_GAP);
    out->width = LX_HOME_CARD_WIDTH;
    out->height = LX_HOME_CARD_HEIGHT;
    return true;
}
