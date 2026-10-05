/* Háromrétegű napi területdiagramok, áttetsző sávokkal és valódi adathézagokkal. */
#include "app_chart.h"
#include <math.h>
#include <stdlib.h>



typedef struct {
    bool consumption;
    bool night;
    bool valid[APP_STATS_SLOT_COUNT];
    float values[3][APP_STATS_SLOT_COUNT];
    uint32_t colors[3];
    float maximum;
    int minute;
} chart_data_t;

/* Az ötperces átlagpont a slot kezdetét jelöli; 24:00-nál nincs fiktív 289. minta. */
static lv_fpoint_t point(const chart_data_t *data, const lv_area_t *area, int series, int slot)
{
    float height = area->y2 - area->y1;
    float zero = area->y2;
    float scale = height / data->maximum;
    return (lv_fpoint_t){
        .x = area->x1 + (area->x2 - area->x1) * (slot * 5.f / 1440.f),
        .y = zero - (series<0?0:data->values[series][slot]) * scale
    };
}

/* Monoton szakasz: nem lő túl a mintákon, a csomópontoknál folytonos az érintő. */
static void curve_to(lv_vector_path_t *path, lv_fpoint_t a, lv_fpoint_t b)
{
    float dx = (b.x - a.x) / 3.f;
    lv_fpoint_t c1 = {a.x + dx, a.y};
    lv_fpoint_t c2 = {b.x - dx, b.y};
    lv_vector_path_cubic_to(path, &c1, &c2, &b);
}

/* A rács és az időjelölő is a sorozatok pontos plot-koordinátáit követi. */
static void line(lv_draw_vector_dsc_t *draw, lv_vector_path_t *path,
                 float x1, float y1, float x2, float y2, uint32_t color, float width)
{
    lv_vector_path_clear(path);
    lv_fpoint_t a = {x1, y1}, b = {x2, y2};
    lv_vector_path_move_to(path, &a);
    lv_vector_path_line_to(path, &b);
    /* ThorVG gradients ignore fill opa; reset the style before stroke-only paths. */
    lv_draw_vector_dsc_set_fill_color(draw, lv_color_hex(0));
    lv_draw_vector_dsc_set_fill_opa(draw, LV_OPA_TRANSP);
    lv_draw_vector_dsc_set_stroke_opa(draw, LV_OPA_COVER);
    lv_draw_vector_dsc_set_stroke_color(draw, lv_color_hex(color));
    lv_draw_vector_dsc_set_stroke_width(draw, width);
    lv_draw_vector_dsc_add_path(draw, path);
}

static void draw_chart(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target_obj(event);
    chart_data_t *data = lv_event_get_user_data(event);
    if(lv_event_get_code(event) == LV_EVENT_DELETE) {
        free(data);
        return;
    }
    if(lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;

    lv_area_t area;
    lv_obj_get_content_coords(obj, &area);
    lv_draw_vector_dsc_t *draw = lv_draw_vector_dsc_create(lv_event_get_layer(event));
    lv_vector_path_t *path = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
    if(!draw || !path) {
        if(draw) lv_draw_vector_dsc_delete(draw);
        if(path) lv_vector_path_delete(path);
        return;
    }
    uint32_t grid = data->night ? 0x29322E : 0xE5EAEC;
    for(int i = 0; i <= 4; i++) {
        float x = area.x1 + (area.x2 - area.x1) * i / 4.f;
        line(draw, path, x, area.y1, x, area.y2, grid, 1.f);
    }
    for(int i = 0; i <= 2; i++) {
        float y = area.y1 + (area.y2 - area.y1) * i / 2.f;
        line(draw, path, area.x1, y, area.x2, y, grid, 1.f);
    }

    /* Minden kitöltés csak a saját alsó és felső határa között van:
     * az áttetsző sávok nem színezik át az alattuk lévő forrásokat. */
    for(int fill=1;fill>=0;fill--){
        for(int series=0;series<3;series++){
            lv_vector_path_clear(path);
            int slot=0;
            while(slot<APP_STATS_SLOT_COUNT){
                while(slot<APP_STATS_SLOT_COUNT && !data->valid[slot])slot++;
                if(slot==APP_STATS_SLOT_COUNT)break;
                int start=slot, end=slot;
                while(end+1<APP_STATS_SLOT_COUNT && data->valid[end+1])end++;
                lv_fpoint_t a=point(data,&area,series,start);
                lv_vector_path_move_to(path,&a);
                for(int i=start+1;i<=end;i++){
                    lv_fpoint_t b=point(data,&area,series,i);
                    float thickness=data->values[series][i]-(series?data->values[series-1][i]:0);
                    float previous=data->values[series][i-1]-(series?data->values[series-1][i-1]:0);
                    if(fill || thickness>0.0001f || previous>0.0001f)curve_to(path,a,b);
                    else lv_vector_path_move_to(path,&b);
                    a=b;
                }
                if(fill){
                    lv_fpoint_t lower=point(data,&area,series-1,end);
                    lv_vector_path_line_to(path,&lower);
                    for(int i=end-1;i>=start;i--){
                        lv_fpoint_t b=point(data,&area,series-1,i);
                        curve_to(path,lower,b); lower=b;
                    }
                    lv_vector_path_close(path);
                }else if(start==end && data->values[series][start]>(series?data->values[series-1][start]:0)){
                    lv_vector_path_append_circle(path,&a,1.f,1.f);
                }
                slot=end+1;
            }
            lv_color_t color=lv_color_hex(data->colors[series]);
            lv_draw_vector_dsc_set_fill_color(draw,color);
            lv_draw_vector_dsc_set_fill_opa(draw,fill?(data->night?LV_OPA_20:LV_OPA_40):LV_OPA_TRANSP);
            lv_draw_vector_dsc_set_stroke_color(draw,color);
            lv_draw_vector_dsc_set_stroke_width(draw,1.6f);
            lv_draw_vector_dsc_set_stroke_opa(draw,fill?LV_OPA_TRANSP:LV_OPA_COVER);
            lv_draw_vector_dsc_add_path(draw,path);
        }
    }
    float now_x = area.x1 + (area.x2 - area.x1) * data->minute / 1440.f;
    line(draw, path, now_x, area.y1, now_x, area.y2, data->night ? 0x56615B : 0xA7B1B5, 1.f);
    lv_draw_vector(draw);
    lv_vector_path_delete(path);
    lv_draw_vector_dsc_delete(draw);
}

lv_obj_t *app_chart_create(lv_obj_t *parent, bool consumption, bool night,
                           uint32_t first_color, uint32_t second_color, uint32_t third_color)
{
    chart_data_t *data = calloc(1, sizeof(*data));
    if(!data) return NULL;
    data->consumption = consumption;
    data->night = night;
    data->maximum = 1.f;
    data->colors[0] = first_color;
    data->colors[1] = second_color;
    data->colors[2] = third_color;
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, draw_chart, LV_EVENT_ALL, data);
    lv_obj_set_user_data(obj, data);
    return obj;
}

void app_chart_update(lv_obj_t *obj, const app_stats_snapshot_t *snapshot,
                      double maximum_kw, int minute_of_day)
{
    if(!obj || !snapshot) return;
    chart_data_t *data = lv_obj_get_user_data(obj);
    data->maximum = fmax(1., maximum_kw);
    data->minute = minute_of_day;
    for(int i = 0; i < APP_STATS_SLOT_COUNT; i++) {
        const app_stats_slot_t *slot=&snapshot->slots[i];
        data->valid[i] = slot->valid && !slot->chart_gap && i * 5 <= minute_of_day;
        const app_energy_split_t *split=&slot->split;
        data->values[0][i] = data->consumption ? split->grid_home : split->pv_home;
        data->values[1][i] = data->values[0][i]+(data->consumption ? split->pv_home : split->pv_export);
        data->values[2][i] = data->values[1][i]+(data->consumption ? split->battery_home : split->pv_stored);
    }
    lv_obj_invalidate(obj);
}
