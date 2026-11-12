from pathlib import Path
import subprocess
import shutil

def edit(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != count:
        raise SystemExit(f'{path}: expected {count} anchors, found {text.count(old)}: {old[:100]}')
    p.write_text(text.replace(old, new))

p = 'main/app_loxone_lighting_model.inc'
edit(p, '''    if (size)
        snprintf(out, size, "%s", text ? text : "");''', '''    if (!size) return;
    if (!text) text = "";
    size_t length = strlen(text);
    size_t copied = length < size ? length : size - 1;
    /* A bájtkorlát nem vághat ketté magyar UTF-8 karaktert. */
    if (copied < length)
        while (copied && ((unsigned char)text[copied] & 0xC0) == 0x80)
            --copied;
    memmove(out, text, copied);
    out[copied] = 0;''')

p = 'main/app_loxone_lighting_ui.inc'
edit(p, '    lv_obj_set_width(label, width);\n', '    lv_obj_set_width(label, width);\n    lv_obj_set_height(label, lv_font_get_line_height(APP_FONT_20));\n')
edit(p, '    lv_obj_set_width(title, 748);\n', '    lv_obj_set_width(title, 748);\n    lv_obj_set_height(title, lv_font_get_line_height(APP_FONT_28));\n')
edit(p, '    home_text(home_labels[slot], 272);\n', '    home_text(home_labels[slot], 272);\n    lv_obj_set_height(home_labels[slot], lv_font_get_line_height(APP_FONT_16));\n')
edit(p, 'static void lighting_ui_refresh_rows(void)\n{\n', '''/* A teljes név a modellben marad; a megjelenítés sortörést sem fogad el. */
static void light_ui_name(lv_obj_t *label, const char *name)
{
    char single[LX_LIGHT_NAME_SIZE];
    snprintf(single, sizeof(single), "%s", name);
    for (unsigned i = 0; single[i]; ++i)
        if ((unsigned char)single[i] < 32 || (unsigned char)single[i] == 127)
            single[i] = ' ';
    label_text_if_changed(label, single);
}

static void lighting_ui_refresh_rows(void)
{
''')
edit(p, '        label_text_if_changed(row->name, circuit.name);\n', '        light_ui_name(row->name, circuit.name);\n')

p = 'main/app_loxone.c'
edit(p, '''        cJSON *answer = valid ? reply(command, true) : NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!answer)''', '''        cJSON *answer = valid ? reply(command, true) : NULL;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!door && request.visibility_revision != s_light_generation)
        {
            /* A nyugta közben elhagyott lap nem tarthat fenn visszajelzésre
             * várást. A már elküldött kérés nem vonható vissza. */
            s_command_pending = false;
            s_command_deadline = 0;
            s_command_status[0] = 0;
        }
        else if (!answer)''')
edit(p, '''    if (s_command_pending && s_command_deadline)
    {
        bool confirmed = false;''', '''    if (s_command_pending && s_command_deadline)
    {
        bool hidden = s_pending_command.visibility_revision != s_light_generation;
        bool confirmed = false;''')
edit(p, '        if (confirmed || now >= s_command_deadline)\n', '        if (hidden || confirmed || now >= s_command_deadline)\n')
edit(p, '                     "Loxone-állapot visszaigazolva." : "Nem érkezett megfelelő Loxone-állapot.");', '                     "Loxone-állapot visszaigazolva." : "Nem érkezett megfelelő Loxone-állapot.");\n            if (hidden) s_command_status[0] = 0;')

p = 'tests/test_loxone_protocol.c'
edit(p, '    /* A minimális asszisztens-pillanatkép ugyanazt a valódi WS-értéktárat olvassa. */', '''    /* Rejtett lapnál nem várunk öt másodpercig olyan kapcsolóválaszra,
     * amelynek állapotát szándékosan már nem követjük. */
    mark_response();
    card->type = LX_HOME_CARD_SWITCH;
    s_home_values.cards[0] = (lx_home_value_t){.value = 0, .valid = true};
    assert(app_loxone_home_command("button", LX_HOME_COMMAND_ON) == ESP_OK);
    home_command_poll(esp_timer_get_time());
    assert(s_command_pending && s_command_deadline);
    app_loxone_home_visible(-1);
    home_command_poll(esp_timer_get_time());
    assert(!s_command_pending && !s_command_deadline);
    card->type = LX_HOME_CARD_PULSE;
    app_loxone_home_visible(0);
    clock_us += 300000;
    lighting_poll(esp_timer_get_time());
    /* A minimális asszisztens-pillanatkép ugyanazt a valódi WS-értéktárat olvassa. */''')

p = 'tests/test_lighting.c'
edit(p, '    free(home);\n', '''    char unicode_names[256] = "{\\"AI1\\":\\"X";
    for (unsigned i = 0; i < 40; ++i) strcat(unicode_names, "é");
    strcat(unicode_names, "\\"}");
    event(block, 6, unicode_names);
    assert(strlen(block->circuits[0].name) == LX_LIGHT_NAME_SIZE - 2);
    assert((unsigned char)block->circuits[0].name[LX_LIGHT_NAME_SIZE - 3] == 0xA9);
    free(home);
''')

p = 'tests/preview/lighting_test.inc'
edit(p, '    assert(lv_obj_get_height(row1->root) == LIGHT_ROW_HEIGHT);\n', '''    assert(lv_obj_get_height(row1->root) == LIGHT_ROW_HEIGHT);
    lv_obj_update_layout(s_screen);
    assert(lv_obj_get_height(row1->name) == lv_font_get_line_height(APP_FONT_20));
    strcpy(app_sim_light_circuits[1].name, "Étkező\\nvilágítás");
    lighting_ui_update();
    assert(!strchr(lv_label_get_text(row1->name), '\\n'));
    assert(strchr(app_sim_light_circuits[1].name, '\\n'));
    strcpy(app_sim_light_circuits[1].name, "Egy hosszabb világítási körnév a nappali mennyezeti részéhez");
    lighting_ui_update();
''')
edit(p, '    save_frame("lighting-color-day");\n', '''    save_frame("lighting-color-day");
    unsigned expanded_invalidations = 0;
    lv_display_add_event_cb(display, count_invalidations, LV_EVENT_INVALIDATE_AREA, &expanded_invalidations);
    lighting_ui_update();
    lv_obj_update_layout(s_screen);
    lv_refr_now(display);
    assert(expanded_invalidations == 0);
    lv_display_remove_event_cb_with_user_data(display, count_invalidations, &expanded_invalidations);
''')

p = 'README.md'
anchor = 'A közvetlen Loxone mód beállítása és jelenlegi ellenőrzési korlátai: [Loxone adatkapcsolat](docs/loxone-direct.md).'
edit(p, anchor, anchor + '''

**Loxone világításvezérlő kártyák:** automatikusan felismert világítási blokkok,
kompakt `− / Ki / +` gombok, aktív világításikép-név és külön kapcsoló-, dimmer-,
RGB/RGBW-kezelő. Csak a megnyitott Otthon-oldal kártyaállapotait dolgozza fel;
a részletes, görgethető ablak egyetlen színválasztót és legfeljebb nyolc élő
körsort tart memóriában. [Beállítás, működés és korlátok](docs/lighting-controller.md).
''')
p = Path('docs/loxone-direct.md')
p.write_text(p.read_text() + '''

## Világításvezérlés és látható Otthon-oldal

A Világításvezérlő kártyához csak a `LightControllerV2` szülőblokkot kell
kiválasztani. A külön körök, a színkezelés és a zárolások automatikusan a
struktúrából érkeznek. [Használat és memóriahatárok](lighting-controller.md).

Az Otthon-kártyák értéktára csak a ténylegesen látható oldalhoz frissül.
Oldalnyitás és részletes blokk megnyitása közös bináris kezdőállapot-kérést
indít a meglévő kapcsolaton. Ez helyi feldolgozási/tárolási szűrés, nem
szerveroldali UUID-előfizetés. Az energia-, fogyasztó-, jelenlét-, postaláda-,
kaputelefon- és Slave-adatok szükséges háttérkövetése változatlan.
Kapcsolatvesztéskor a világítási ablak a türelmi idő előtt bezáródik,
a függő világítási parancsok nem játszódnak vissza.
''')

p = '.github/workflows/ci-shared.yml'
edit(p, '          ZOLEE_PREVIEW_RGB565=1 ../.preview-build/ui_preview\n', '''          ZOLEE_PREVIEW_RGB565=1 ../.preview-build/ui_preview
          ZOLEE_LIGHTING_ONLY=1 ../.preview-build/ui_preview
          ZOLEE_LIGHTING_ONLY=1 ZOLEE_PREVIEW_RGB565=1 ../.preview-build/ui_preview
''')
# A fejlesztői munkaterület nem kerül a kész PR-ba. A futó Actions-job
# már beolvasott konfigurációja ettől még elvégzi a tesztet és a commitot.
for path in ('.github/workflows/lighting-development.yml', '.github/workflows/lighting-dependencies.yml'):
    Path(path).unlink(missing_ok=True)
subprocess.run(['git', 'add', '.github/workflows'], check=True)
formatter = shutil.which('clang-format')
if formatter:
    paths = list(Path('main').glob('app_loxone_lighting*')) + [
        Path('simulator/lighting_fixture.h'), Path('simulator/lighting_fixture.inc'),
        Path('tests/test_lighting_service.inc'), Path('tests/preview/lighting_test.inc')]
    subprocess.run([formatter, '-i', *map(str, paths)], check=True)
subprocess.run(['python3', 'tools/prepare_web.py'], check=True)
with open('/tmp/lighting-regression.log', 'w') as log:
    result = subprocess.run(['python3', 'tests/run_tests.py'], stdout=log, stderr=subprocess.STDOUT)
print('\n'.join(Path('/tmp/lighting-regression.log').read_text().splitlines()[-100:]))
if result.returncode:
    raise SystemExit(result.returncode)
print('Full regression passed; finishing native lighting checks before committing.')
