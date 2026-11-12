from pathlib import Path
import subprocess

# A korábban feltöltött integráció pontos, commitazonosított változata.
# A segéd sikeres natív teszt után automatikusan törlődik az ágról.
source_sha = '649a8e9a85fbdf2eb0df22267a66d377c6f5bb6d'
subprocess.run(['git', 'fetch', '--depth=1', 'origin', source_sha], check=True)
source = subprocess.check_output(['git', 'show', source_sha + ':tools/.lighting-edit.py'], text=True)
old = '''edit(p, \'''        light_edit = circuit.actual;
        lv_obj_remove_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
\''', \'''        if (row != light_picker_row) light_edit = circuit.actual;
        lv_obj_remove_flag(light_body, LV_OBJ_FLAG_SCROLLABLE);
\''', count=1)'''
new = '''edit(p, \'''        light_touch_index = (int)row->index;
        light_edit = circuit.actual;
\''', \'''        light_touch_index = (int)row->index;
        if (row != light_picker_row) light_edit = circuit.actual;
\''')'''
assert source.count(old) == 1
source = source.replace(old, new)
old = "subprocess.run(['node', '--check', 'assets/web/loxone-home.js'], check=True)"
new = "subprocess.run(['node', '--check'], input=Path('assets/web/loxone-home.js').read_text().replace('/*HOME_ICONS*/', '[]'), text=True, check=True)"
assert source.count(old) == 1
source = source.replace(old, new)
exec(compile(source, '<verified-lighting-integration>', 'exec'))
