#!/usr/bin/env python3
"""Regressziós védőtesztek a két eredeti, felhasználó által jelzett hibához.

A statikus tesztek kiegészítik a tényleges LVGL renderelő és C modell
CI ellenőrzését; nem helyettesítik a hardveres megtekintést.
"""
from pathlib import Path
import re


def includes_code(source, fragment):
    """Az ellenőrzött C-kódrészletet szóköz- és sortörésfüggetlenül keresd."""
    compact = lambda text: re.sub(r"\\s+", "", text)
    return compact(fragment) in compact(source)

ROOT = Path(__file__).resolve().parents[1]
chart = (ROOT / "main/app_chart.c").read_text(encoding="utf-8")
ui = (ROOT / "main/app_ui.c").read_text(encoding="utf-8")
templates = (ROOT / "main/app_template_ui.inc").read_text(encoding="utf-8")
flow = (ROOT / "main/app_flow_model.h").read_text(encoding="utf-8")

# A kitöltés saját színt kap a sorozatból, és nem szürke fix színt.
assert includes_code(chart, "lv_color_hex(data->colors[series])")
assert includes_code(chart, "lv_draw_vector_dsc_set_fill_color(draw,color)")
assert includes_code(chart, "lv_draw_vector_dsc_set_fill_opa(draw,fill?")
assert includes_code(chart, "LV_OPA_60:LV_OPA_70")
# Ugyanaz a rajzoló szolgálja ki a Rendszer és a Napi mérleg témát.
assert includes_code(templates, "app_chart_create_kind")
assert includes_code(templates, "app_chart_update(node->obj,stats")

# Minden animált energiaút ugyanazt a teljesítményfüggő modellt használja.
assert includes_code(ui, "app_flow_advance(f->phase,kw,forward,dt)")
assert includes_code(templates, "bool forward=node->power>=0;")
assert includes_code(templates, 'cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"reverse"))')
assert includes_code(templates, "app_flow_advance(node->phase,fabs(node->power),forward,dt)")
assert includes_code(templates, 'int dot_count=tn(node->spec,"dot_count",4);')
assert includes_code(flow, "fmin(fabs(kw)/7.,1.)")
assert includes_code(flow, ".045f+.955f*load")
assert includes_code(templates, 'tn(node->spec,"inactive_opacity",thick?72:86)')
assert includes_code(templates, 'st->yesterday_comparison_partial')
assert includes_code(templates, 'st->comparison_today_totals')
assert includes_code(templates, 'st->comparison_yesterday_totals')
assert includes_code(templates, 'fmt_compared(out,128,t,y,st->yesterday_comparison_partial)')
assert includes_code(ui, 'comparison_today_totals')
assert includes_code(ui, 'st->comparison_ready')
assert includes_code(templates, 'if(!st->comparison_ready)')
assert includes_code(ui, 'fmt_compared(')
stats = (ROOT / 'main/app_stats.c').read_text(encoding='utf-8')
assert includes_code(stats, 'app_stats_compare_slots(')
assert includes_code(stats, 'out->comparison_paired_slots=match.paired_slots')
assert includes_code(stats, 'match.paired_slots>=minimum')
assert includes_code(templates, 'lv_obj_set_size(node->detail,width,height)')
assert includes_code(flow, "if(fabs(kw)<0.0005 || seconds<=0)return phase;")

print("PASS: chart colors, reversible energy paths and power-proportional flow animation")
