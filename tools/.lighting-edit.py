from pathlib import Path
import subprocess

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
edit('main/app_loxone_lighting_ui.inc', 'APP_FONT_24', 'APP_FONT_28')
edit('assets/web/loxone-home.js', "note.style.gridColumn = '1 / -1'; grid.append(note);", "note.setAttribute('style', 'grid-column:1/-1'); grid.append(note);")
p = 'tests/test_loxone_home_web.js'
anchor = "    console.log('PASS: actual home editor"
test = r'''    /* A világításkártyához csak szülőblokk kell, külön visszajelző UUID nem. */
    const lightControl = '10000000-0000-0000-0000000000000001';
    const lightCatalog = [...catalog, {uuid:'10000000-0000-0000-0000000000000004',
        control_uuid:lightControl, action_uuid:lightControl, name:'Nappali <b>világítás</b>',
        type:'LightControllerV2', secured:false, room:'Nappali', category:'Világítás', lighting:'{}'}];
    state = {schema:2, consumers:[], pages:[{id:'lighting-page', title:'Világítás', cards:[{
        id:'lighting-test', name:'Nappali világítás', icon:icons[0].id, type:'lighting',
        control_uuid:lightControl}]}], intercom:{}, revision:0, busy:false, status:''};
    const lightingContext = {...sandbox}; vm.createContext(lightingContext);
    vm.runInContext(fs.readFileSync('assets/web/loxone-home.js','utf8').replace('/*HOME_ICONS*/',JSON.stringify(icons))+'\nglobalThis.home=LoxoneHome;',lightingContext);
    await lightingContext.home.refresh(lightCatalog);
    const lightingSelect = nodes.get('home-cards-lighting-test-control_uuid');
    assert(lightingSelect.children.some(option => option.value === lightControl));
    assert(!lightingSelect.children.some(option => option.value === control));
    assert(!nodes.has('home-cards-lighting-test-uuid'));
    assert(nodes.get('home-cards-lighting-test-type').children.some(option => option.value === 'lighting'));
    const lightingPosts = posts.length;
    await click('home-save');
    assert.equal(posts.length, lightingPosts + 1);
    assert.equal(posts.at(-1).pages[0].cards[0].type, 'lighting');
    assert.equal(posts.at(-1).pages[0].cards[0].control_uuid, lightControl);
    edit('home-room', JSON.stringify('Kert'), 'onchange');
    assert(nodes.get('home-cards-lighting-test-control_uuid').children.some(option => option.value === lightControl));
    assert.equal(nodes.get('home-cards-lighting-test-name').value, 'Nappali világítás');
    console.log('PASS: lighting block auto-discovery, no per-circuit/state mapping, save and retained filtered selection');
'''
s = Path(p).read_text()
assert s.count(anchor) == 1
Path(p).write_text(s.replace(anchor, test + anchor))
subprocess.run(['node', 'tests/test_loxone_home_web.js'], check=True)
