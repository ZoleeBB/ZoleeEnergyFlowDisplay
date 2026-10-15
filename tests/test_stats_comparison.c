/* Synthetic five-minute energy histories, no device or SD required. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#define APP_STATS_SLOT_COUNT 288
typedef struct {
    double production_kwh, consumption_kwh, grid_import_kwh, grid_export_kwh,
        battery_charge_kwh, battery_discharge_kwh, pv_home_kwh,
        pv_export_kwh, pv_stored_kwh, grid_home_kwh, battery_home_kwh;
} app_energy_totals_t;
typedef struct {bool valid;uint32_t sample_count;bool chart_gap;
    app_energy_totals_t energy;} app_stats_slot_t;
#include "app_stats_compare.h"

static app_stats_slot_t today[288], yesterday[288];
static void reset(void) {for(int i=0;i<288;i++){
    today[i]=(app_stats_slot_t){0};yesterday[i]=(app_stats_slot_t){0};}}
static void fill(int from,int to,double a,double b){
    for(int i=from;i<to;i++){
        today[i].valid=yesterday[i].valid=true;
        today[i].sample_count=yesterday[i].sample_count=50;
        today[i].energy.production_kwh=a;
        yesterday[i].energy.production_kwh=b;
        today[i].energy.consumption_kwh=2*a;
        yesterday[i].energy.consumption_kwh=2*b;
        today[i].energy.pv_home_kwh=a/2;
        yesterday[i].energy.pv_home_kwh=b/2;
    }
}
static void close_to(double x,double y){assert(fabs(x-y)<1e-8);}
int main(void){
    reset();fill(0,228,0.12,0.10);fill(228,288,100,100);
    app_stats_comparison_result_t r=app_stats_compare_slots(today,yesterday,228);
    assert(r.paired_slots==228&&!r.partial);
    close_to(r.today.production_kwh,27.36);
    close_to(r.yesterday.production_kwh,22.8);
    close_to(r.today.pv_home_kwh,13.68);
    /* Yesterday starts at noon; compare only overlapping slots, not full today. */
    reset();fill(144,228,0.12,0.10);
    for(int i=0;i<144;i++){
        today[i].valid=true;today[i].sample_count=55;
        today[i].energy.production_kwh=0.8;
    }
    r=app_stats_compare_slots(today,yesterday,228);
    assert(r.paired_slots==84&&r.partial);
    close_to(r.today.production_kwh,10.08);
    close_to(r.yesterday.production_kwh,8.4);
    yesterday[150].chart_gap=true;
    r=app_stats_compare_slots(today,yesterday,228);
    assert(r.paired_slots==84&&r.partial);
    close_to(r.today.production_kwh,10.08);
    /* A chart_gap flag alone is not missing data: retain the measured
     * energy and mark the comparison approximate even with full coverage. */
    reset();fill(0,36,0.12,0.10);
    yesterday[7].chart_gap=true;
    r=app_stats_compare_slots(today,yesterday,36);
    assert(r.paired_slots==36&&r.partial);
    close_to(r.today.production_kwh,4.32);
    /* Legacy persisted slot: integration exists but valid/count were zero. */
    reset();today[0].energy.grid_import_kwh=0.1;
    yesterday[0].energy.grid_import_kwh=0.2;
    r=app_stats_compare_slots(today,yesterday,1);
    assert(r.paired_slots==1&&!r.partial);
    close_to(r.today.grid_import_kwh,0.1);
    /* No matching time slots means no valid comparison. */
    reset();today[2].valid=true;today[2].sample_count=30;
    yesterday[3].valid=true;yesterday[3].sample_count=30;
    r=app_stats_compare_slots(today,yesterday,48);
    assert(r.paired_slots==0&&r.partial);
    /* Zero night production with valid samples is a real measurement. */
    reset();fill(0,36,0,0);
    r=app_stats_compare_slots(today,yesterday,36);
    assert(r.paired_slots==36&&!r.partial);
    close_to(r.today.production_kwh,0);
    printf("PASS: complete and partial days, gaps, legacy energy, no overlap, zeros and 19:00 cutoff\n");
    return 0;
}
