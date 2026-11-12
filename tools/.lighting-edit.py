from pathlib import Path
import subprocess

def edit(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != count:
        raise SystemExit(f'{path}: expected {count} anchors, found {text.count(old)}: {old[:120]}')
    p.write_text(text.replace(old, new))

p = 'main/app_ui.c'
edit(p, 'static void clear(void)\n{\n', '''static bool lighting_ui_is_open(void);
static void lighting_ui_reset(void);

static void clear(void)
{
    lighting_ui_reset();
    app_loxone_home_visible(-1);
''')
edit(p, '''        master_display_update(have, s.night_mode, intercom_active);
''', '''        master_display_update(have, s.night_mode, intercom_active);
        /* A felugró nézet a türelmi idő alatt sem maradhat vezérelhető.
         * A kaputelefon elsőbbsége még a rendes Otthon-frissítés előtt él. */
        if ((!rt.loxone_connected || intercom_active) && bsp_display_lock(-1) == ESP_OK)
        {
            lighting_ui_reset();
            app_loxone_home_visible(-1);
            bsp_display_unlock();
        }
''')

p = 'main/app_loxone_home_ui.inc'
edit(p, 'static void home_gesture(lv_dir_t direction)\n{\n', 'static void home_gesture(lv_dir_t direction)\n{\n    if (lighting_ui_is_open()) return;\n')
edit(p, 'static bool home_prepare(bool night)\n{\n', '''static bool home_prepare(bool night)
{
    /* A megnyitott kezelőt egy NightMode-váltás nem dobja el. A fényerő
     * továbbra is követi az éjszakai módot; a stílus bezárás után frissül. */
    if (night != home_night && bsp_display_lock(-1) == ESP_OK)
    {
        if (lighting_ui_is_open()) night = home_night;
        bsp_display_unlock();
    }
''')
edit(p, 'static void home_build(void)\n', '#include "app_loxone_lighting_ui.inc"\n\nstatic void home_build(void)\n')
edit(p, '    memset(home_buttons, 0, sizeof(home_buttons));\n', '    memset(home_buttons, 0, sizeof(home_buttons));\n    memset(light_card_buttons, 0, sizeof(light_card_buttons));\n')
edit(p, '''        bool control = home_page == 2 && home_config.cards[index].type != LX_HOME_CARD_VALUE;
''', '''        if (home_page == 2 && home_config.cards[index].type == LX_HOME_CARD_LIGHTING)
        {
            lighting_card_build(card, title, i, index);
            continue;
        }
        bool control = home_page == 2 && home_config.cards[index].type != LX_HOME_CARD_VALUE;
''')
edit(p, '    if (s_view != VIEW_HOME) home_build();\n', '    if (s_view != VIEW_HOME) home_build();\n    app_loxone_home_visible(home_page == 2 ? (int)home_card_page : -1);\n')
edit(p, '        const lx_home_card_t *card = &home_config.cards[index];\n', '''        const lx_home_card_t *card = &home_config.cards[index];
        if (home_page == 2 && card->type == LX_HOME_CARD_LIGHTING)
        {
            lighting_card_update(i, index, home_connected && !loxone_display_is_held());
            continue;
        }
''')
edit(p, '''                          home_status[0] ? home_feedback() : tr("Élő Loxone-adatok", "Live Loxone data"));
    return true;''', '''                          home_status[0] ? home_feedback() : tr("Élő Loxone-adatok", "Live Loxone data"));
    lighting_ui_update();
    return true;''')

p = 'main/app_loxone_lighting_ui.inc'
edit(p, 'static bool light_touch, light_syncing, light_refreshing, light_ui_error;', 'static bool light_touch, light_syncing, light_refreshing, light_ui_error;\nstatic bool light_rows_failed, light_wheel_failed;')
edit(p, '    light_picker_row = NULL;\n', '    light_picker_row = NULL;\n    light_wheel_failed = false;\n')
edit(p, '    light_syncing = light_refreshing = light_ui_error = false;\n', '    light_syncing = light_refreshing = light_ui_error = light_rows_failed = false;\n')
edit(p, '    if (light_wheel) return true;\n', '    if (light_wheel) return true;\n    if (light_wheel_failed) return false;\n')
edit(p, '    if (!light_wheel_pixels) return false;\n', '    if (!light_wheel_pixels) { light_wheel_failed = true; return false; }\n')
edit(p, '''        light_edit = circuit.actual;
        lv_obj_remove_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
''', '''        if (row != light_picker_row) light_edit = circuit.actual;
        lv_obj_remove_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
''', count=1)
# A tárcsánál ugyanaz a mód marad, amelyet az érintés kezdetekor látott a felhasználó.
edit(p, '''        light_touch_index = (int)light_picker_row->index;
        light_edit = circuit.actual;
''', '''        light_touch_index = (int)light_picker_row->index;
''')
edit(p, '    if (!light_rows)\n    {\n        light_rows = light_ui_alloc', '''    if (!light_rows && light_rows_failed)
    {
        label_text_if_changed(light_status, tr("Nincs elegendő memória. Zárd be és nyisd újra.", "Not enough memory. Close and reopen."));
        return;
    }
    if (!light_rows)
    {
        light_rows = light_ui_alloc''')
edit(p, '''        if (!light_rows)
        {
            label_text_if_changed''', '''        if (!light_rows)
        {
            light_rows_failed = true;
            label_text_if_changed''')

p = 'simulator/home_fixture.c'
edit(p, '#include "home_fixture.h"\n', '#include "home_fixture.h"\n#include "lighting_fixture.h"\n')
edit(p, 'void app_sim_home_reset(void)\n{\n', 'void app_sim_home_reset(void)\n{\n    app_sim_lighting_reset();\n')
p = Path(p)
p.write_text(p.read_text() + '\n#include "lighting_fixture.inc"\n')

p = 'assets/web/loxone-home.js'
edit(p, 'function stateChoices(control = false, pulse = false)', 'function stateChoices(control = false, pulse = false, lighting = false)')
edit(p, '''            if (control && !(item.secured === false && (item.type === 'Switch' ||
                (pulse && ['Pushbutton', 'TimedSwitch'].includes(item.type))))) continue;''', '''            if (lighting && (item.type !== 'LightControllerV2' || !item.lighting)) continue;
            if (control && !lighting && !(item.secured === false && (item.type === 'Switch' ||
                (pulse && ['Pushbutton', 'TimedSwitch'].includes(item.type))))) continue;''')
edit(p, "['switch', 'Be/ki kapcsoló']]);", "['switch', 'Be/ki kapcsoló'], ['lighting', 'Világításvezérlő']]);")
edit(p, "                    if (entry.type !== 'pulse') source(grid, entry);", "                    if (!['pulse', 'lighting'].includes(entry.type)) source(grid, entry);")
edit(p, "                        select(grid, 'Vezérelhető Loxone objektum', entry, 'control_uuid', stateChoices(true, entry.type === 'pulse'));", '''                        select(grid, entry.type === 'lighting' ? 'Loxone világításvezérlő blokk' : 'Vezérelhető Loxone objektum',
                            entry, 'control_uuid', stateChoices(true, entry.type === 'pulse', entry.type === 'lighting'));
                    if (entry.type === 'lighting') {
                        const note = create('p', 'A világítási körök automatikusan, a Loxone sorrendjében jelennek meg. Csak a megnyitott Otthon-oldal adatait dolgozzuk fel.');
                        note.style.gridColumn = '1 / -1'; grid.append(note);
                    }''')
edit(p, "            if (entry.type !== 'pulse' && (!uuidValid(entry.source.uuid)", "            if (!['pulse', 'lighting'].includes(entry.type) && (!uuidValid(entry.source.uuid)")

p = 'tests/preview/preview.c'
edit(p, 'int main(void)\n', '#include "lighting_test.inc"\n\nint main(void)\n')
edit(p, '    if (getenv("ZOLEE_HUMIDITY_ONLY") ||', '    if (getenv("ZOLEE_LIGHTING_ONLY") || getenv("ZOLEE_HUMIDITY_ONLY") ||')
edit(p, '        if (getenv("ZOLEE_ENERGY_DISABLED_ONLY"))\n', '        if (getenv("ZOLEE_LIGHTING_ONLY"))\n            test_lighting_ui(runtime, sample);\n        else if (getenv("ZOLEE_ENERGY_DISABLED_ONLY"))\n')
edit('tests/preview/lighting_test.inc', ' == 204);', ' == LX_HOME_CARD_HEIGHT);')
subprocess.run(['node', '--check', 'assets/web/loxone-home.js'], check=True)
print('Integrated lighting cards, visible-page notifications, modal, web selection and actual LVGL tests.')
