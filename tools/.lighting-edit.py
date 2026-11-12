from pathlib import Path
import subprocess

subprocess.run(['sudo', 'apt-get', 'install', '-y', 'python3-numpy', 'python3-yaml'], check=True)
source_sha = '667201c78424a92d583aa72248e73b1a15f986f6'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
start = source.index("p = 'main/app_loxone_lighting_model.inc'")
end = source.index("p = 'main/app_loxone_lighting_ui.inc'", start)
source = source[:start] + source[end:]
extra = r'''
p = 'main/app_loxone_lighting_ui.inc'
edit(p, '    bool bound;\n', '    bool bound;\n    char name_cache[LX_LIGHT_NAME_SIZE];\n')
edit(p, 'static lv_obj_t *light_card_buttons[LX_HOME_MAX_CARDS][3];\n', ''' + repr('''static lv_obj_t *light_card_buttons[LX_HOME_MAX_CARDS][3];
/* Az LVGL DOTS mód a saját szövegét átírja. A 12×128 bájtos, külső RAM-os
 * forrásszöveg-cache megakadályozza az állandó újrarajzolást hosszú neveknél. */
static TEMPLATE_RAM char light_card_text[LX_HOME_MAX_CARDS][128];
''') + r''')
edit(p, 'static void light_ui_name(lv_obj_t *label, const char *name)\n{\n', ''' + repr('''static void light_ui_name(light_ui_row_t *row, const char *name)
{
    if (!strcmp(row->name_cache, name)) return;
    snprintf(row->name_cache, sizeof(row->name_cache), "%s", name);
''') + r''')
edit(p, '    label_text_if_changed(label, single);\n', '    lv_label_set_text(row->name, single);\n')
edit(p, '        light_ui_name(row->name, circuit.name);\n', '        light_ui_name(row, circuit.name);\n')
edit(p, '    home_text(home_labels[slot], 272);\n', '    home_text(home_labels[slot], 272);\n    light_card_text[slot][0] = 0;\n')
edit(p, '    label_text_if_changed(home_labels[slot], text);\n', ''' + repr('''    if (strcmp(light_card_text[slot], text))
    {
        snprintf(light_card_text[slot], sizeof(light_card_text[slot]), "%s", text);
        lv_label_set_text(home_labels[slot], text);
    }
''') + r''')
p = 'tests/preview/lighting_test.inc'
edit(p, '    save_frame("lighting-cards-day");\n', ''' + repr('''    strcpy(app_sim_light_summary.name, "Egy nagyon hosszú világítási kép neve a nappali esti megvilágításához");
    lighting_card_update(0, 0, true);
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    unsigned card_invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &card_invalidations);
    lighting_card_update(0, 0, true);
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    assert(card_invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &card_invalidations);
    strcpy(app_sim_light_summary.name, "TV nézés");
    lighting_card_update(0, 0, true);
    save_frame("lighting-cards-day");
''') + r''')
'''
# A C-formázás és a teljes regresszió előtt, már az egysoros címkéken javítunk.
anchor = "p = 'README.md'"
assert source.count(anchor) == 1
source = source.replace(anchor, extra + '\n' + anchor)
anchor = "# A fejlesztői munkaterület nem kerül a kész PR-ba."
extra_ci = """edit('.github/workflows/ci-shared.yml',
    'sudo apt-get update && sudo apt-get install -y libcjson-dev zlib1g-dev libmbedtls-dev',
    'sudo apt-get update && sudo apt-get install -y libcjson-dev zlib1g-dev libmbedtls-dev python3-numpy python3-pil', count=2)
"""
preview_step = '''
      - name: Preserve actual lighting UI renders
        uses: actions/upload-artifact@v4
        with:
          name: zolee-lighting-previews-${{ inputs.source_sha }}
          retention-days: 7
          overwrite: true
          path: .preview-images/lighting-*.ppm
          if-no-files-found: error
'''
extra_ci += "edit('.github/workflows/ci-shared.yml', '\\n  template-packages:', " + repr(preview_step + '\n  template-packages:') + ")\n"
assert source.count(anchor) == 1
source = source.replace(anchor, extra_ci + anchor)
exec(compile(source, '<lighting-final-regression>', 'exec'))
