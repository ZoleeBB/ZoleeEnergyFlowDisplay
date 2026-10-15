#!/usr/bin/env python3
"""Regressziós védőtesztek a két eredeti, felhasználó által jelzett hibához.

A statikus tesztek kiegészítik a tényleges LVGL renderelő és C modell
CI ellenőrzését; nem helyettesítik a hardveres megtekintést.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
chart = (ROOT / "main/app_chart.c").read_text(encoding="utf-8")
ui = (ROOT / "main/app_ui.c").read_text(encoding="utf-8")
templates = (ROOT / "main/app_template_ui.inc").read_text(encoding="utf-8")
flow = (ROOT / "main/app_flow_model.h").read_text(encoding="utf-8")

# A kitöltés saját színt kap a sorozatból, és nem szürke fix színt.
assert "lv_color_hex(data->colors[series])" in chart
assert "lv_draw_vector_dsc_set_fill_color(draw,color)" in chart
assert "lv_draw_vector_dsc_set_fill_opa(draw,fill?" in chart
assert "LV_OPA_60:LV_OPA_70" in chart
# Ugyanaz a rajzoló szolgálja ki a Rendszer és a Napi mérleg témát.
assert "app_chart_create_kind" in templates
assert "app_chart_update(node->obj,stats" in templates

# Minden animált energiaút ugyanazt a teljesítményfüggő modellt használja.
assert "app_flow_advance(f->phase,kw,forward,dt)" in ui
assert "bool forward=node->power>=0;" in templates
assert 'cJSON_IsTrue(cJSON_GetObjectItem(node->spec,"reverse"))' in templates
assert "app_flow_advance(node->phase,fabs(node->power),forward,dt)" in templates
assert 'int dot_count=tn(node->spec,"dot_count",4);' in templates
assert "fmin(fabs(kw)/7.,1.)" in flow
assert ".045f+.955f*load" in flow
assert 'tn(node->spec,"inactive_opacity",thick?72:86)' in templates
assert 'st->yesterday_comparison_partial' in templates
assert 'st->comparison_today_totals' in templates
assert 'st->comparison_yesterday_totals' in templates
assert 'fmt_compared(out,128,t,y,st->yesterday_comparison_partial)' in templates
assert 'comparison_today_totals' in ui
assert 'st->comparison_ready' in ui
assert 'if(!st->comparison_ready)' in templates
assert 'fmt_compared(' in ui
stats = (ROOT / 'main/app_stats.c').read_text(encoding='utf-8')
assert 'app_stats_compare_slots(' in stats
assert 'out->comparison_paired_slots=match.paired_slots' in stats
assert 'match.paired_slots>=minimum' in stats
assert 'lv_obj_set_size(node->detail,width,height)' in templates
assert "if(fabs(kw)<0.0005 || seconds<=0)return phase;" in flow

print("PASS: chart colors, reversible energy paths and power-proportional flow animation")
