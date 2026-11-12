from pathlib import Path
import subprocess

source_sha = '3cc2adb3e27c40ecb670d000c0e2fd25f292c946'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
# A mérés csak a desktop tesztben él, a firmware forrását nem műszerezi.
addition = r'''
p = Path('tests/preview/lighting_test.inc')
s = p.read_text()
trace = ''' + repr('''static void lighting_trace_area(lv_event_t *event)
{
    const lv_area_t *area = lv_event_get_param(event);
    fprintf(stderr, "LIGHT INVALIDATE: %d,%d to %d,%d\\n", (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
}

''') + r'''
s = trace + s
s = s.replace('    lighting_ui_update();\n    lv_obj_update_layout(s_screen);\n    lv_refr_now(display);\n    assert(expanded_invalidations == 0);', ''' + repr('''    lv_display_add_event_cb(display, lighting_trace_area, LV_EVENT_INVALIDATE_AREA, NULL);
    fprintf(stderr, "TRACE BEFORE UPDATE\\n");
    lighting_ui_update();
    fprintf(stderr, "TRACE AFTER UPDATE %u\\n", expanded_invalidations);
    lv_obj_update_layout(s_screen);
    fprintf(stderr, "TRACE AFTER LAYOUT %u\\n", expanded_invalidations);
    lv_refr_now(display);
    fprintf(stderr, "TRACE AFTER REFRESH %u\\n", expanded_invalidations);
    lv_display_remove_event_cb_with_user_data(display, lighting_trace_area, NULL);
    assert(expanded_invalidations == 0);''') + r''')
p.write_text(s)
'''
# A rögzített csomag végrehajtása előtt toldjuk a natív tesztdiagnosztikát az editcsomagba.
needle = "exec(compile(source, '<lighting-final-regression>', 'exec'))"
assert source.count(needle) == 1
replacement = "source = source.replace(\"formatter = shutil.which('clang-format')\", " + repr(addition + "\nformatter = shutil.which('clang-format')") + ")\n" + needle
source = source.replace(needle, replacement)
exec(compile(source, '<lighting-native-diagnostics>', 'exec'))
