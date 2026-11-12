import subprocess

source_sha = '3cc2adb3e27c40ecb670d000c0e2fd25f292c946'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
addition = '''
edit('main/app_loxone_lighting_ui.inc',
    '    if (!light_picker) return;\\n',
    '    if (!light_picker) return;\\n'
    '    /* A kompakt sor színmintája is ugyanabban a képkockában kövesse a húzást. */\\n'
    '    if (light_picker_row && light_picker_row->swatch)\\n'
    '        bg_color_if_changed(light_picker_row->swatch, c(light_ui_color(&light_edit)), 0);\\n')
edit('main/app_loxone_lighting_ui.inc',
    '    light_ui_hidden(light_temperature, !white);\\n',
    '    /* A gombcímke saját fehér színt örökölt a PIN-kezelőből;\\n'
    '     * a világos, inaktív módnál közvetlenül a címkét kell igazítani. */\\n'
    '    text_color_if_changed(lv_obj_get_child(light_mode_buttons[0], 0),\\n'
    '                          c(!white ? 0xFFFFFF : light_ui_foreground()), 0);\\n'
    '    text_color_if_changed(lv_obj_get_child(light_mode_buttons[1], 0),\\n'
    '                          c(white ? 0xFFFFFF : light_ui_foreground()), 0);\\n'
    '    light_ui_hidden(light_temperature, !white);\\n')
edit('tests/preview/lighting_test.inc',
    '    assert(light_expanded == 2 && light_picker_row == row2 && light_wheel_pixels);\\n',
    '    assert(light_expanded == 2 && light_picker_row == row2 && light_wheel_pixels);\\n'
    '    assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(light_mode_buttons[0], 0), 0), c(0xFFFFFF)));\\n'
    '    assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(light_mode_buttons[1], 0), 0), c(light_ui_foreground())));\\n')
'''
needle = "exec(compile(source, '<lighting-final-regression>', 'exec'))"
assert source.count(needle) == 1
replacement = "source = source.replace(\"formatter = shutil.which('clang-format')\", " + repr(addition + "\nformatter = shutil.which('clang-format')") + ")\n" + needle
source = source.replace(needle, replacement)
exec(compile(source, '<lighting-final-ui>', 'exec'))
