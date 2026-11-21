#pragma once
typedef struct lv_display_t lv_display_t;
typedef struct lv_obj_t lv_obj_t;
typedef struct lv_timer_t lv_timer_t;
lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), unsigned interval, void *arg);
lv_obj_t *lv_screen_active(void);
