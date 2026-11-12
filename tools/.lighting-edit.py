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
'''
needle = "exec(compile(source, '<lighting-final-regression>', 'exec'))"
assert source.count(needle) == 1
replacement = "source = source.replace(\"formatter = shutil.which('clang-format')\", " + repr(addition + "\nformatter = shutil.which('clang-format')") + ")\n" + needle
source = source.replace(needle, replacement)
exec(compile(source, '<lighting-final-swatch>', 'exec'))
