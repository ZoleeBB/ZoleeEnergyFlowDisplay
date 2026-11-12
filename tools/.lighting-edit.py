from pathlib import Path
import subprocess

def edit(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    if text.count(old) != count:
        raise SystemExit(f'{path}: integration anchor mismatch: {old[:100]}')
    p.write_text(text.replace(old, new))

p = Path('main/app_loxone.h')
s = p.read_text()
assert 'app_loxone_lighting_service.h' not in s
p.write_text(s + '\n#include "app_loxone_lighting_service.h"\n')
p = 'main/app_loxone.c'
edit(p, '    unsigned revision, session, intercom_revision;\n', '    unsigned revision, session, intercom_revision, visibility_revision;\n')
edit(p, 'static void invalidate(bool configured)\n', '#include "app_loxone_lighting_service.inc"\n\nstatic void invalidate(bool configured)\n')
edit(p, '    s_home_live = false;\n', '    s_home_live = false;\n    lighting_invalidate_locked();\n')
edit(p, '            home_command_poll(now);\n', '            home_command_poll(now);\n            if (!s_dead) lighting_poll(esp_timer_get_time());\n')
edit(p, '    bool ok = !s_home || lx_home_values_update(&s_home_values, s_home, type, data, size);', '''    int visible_page = s_light_page;
    unsigned visible_first, visible_count;
    if (visible_page >= 0 && !lx_home_page_range(s_home, (unsigned)visible_page, &visible_first, &visible_count))
        visible_page = -1;
    bool ok = !s_home || lx_home_values_update_visible(&s_home_values, s_home, visible_page, type, data, size);
    if (ok) ok = lighting_events_locked(type, data, size);''')
edit(p, '''    char allowed_command[96];
    for (unsigned i = 0; i < s_home->card_count; i++)
        values->command_supported[i] = lx_home_command_build(s_home, s_catalog,
            s_home->cards[i].id, s_home->cards[i].type == LX_HOME_CARD_PULSE ?
            LX_HOME_COMMAND_PULSE : LX_HOME_COMMAND_ON, allowed_command);''', '''    char allowed_command[96];
    memset(values->command_supported, 0, sizeof(values->command_supported));
    unsigned first = 0, count = 0;
    if (s_light_page >= 0)
        (void)lx_home_page_range(s_home, (unsigned)s_light_page, &first, &count);
    for (unsigned i = first; i < first + count; ++i)
    {
        lx_home_command_t action = s_home->cards[i].type == LX_HOME_CARD_PULSE ? LX_HOME_COMMAND_PULSE :
            s_home->cards[i].type == LX_HOME_CARD_LIGHTING ? LX_HOME_COMMAND_LIGHT_NEXT : LX_HOME_COMMAND_ON;
        values->command_supported[i] = lighting_home_command_allowed_locked(s_home->cards[i].id) &&
            lx_home_command_build(s_home, s_catalog, s_home->cards[i].id, action, allowed_command);
    }''')
edit(p, '    bool allowed = (!check_generation || s_intercom_generation == generation) &&\n', '    bool allowed = lighting_home_command_allowed_locked(card_id) &&\n                   (!check_generation || s_intercom_generation == generation) &&\n')
edit(p, '    request.intercom_revision = s_intercom_generation;\n', '    request.intercom_revision = s_intercom_generation;\n    request.visibility_revision = s_light_generation;\n')
edit(p, '                     request.session == s_home_session && now >= request.queued_us &&\n', '                     request.session == s_home_session &&\n                     (door || request.visibility_revision == s_light_generation) &&\n                     lighting_home_command_allowed_locked(request.id) && now >= request.queued_us &&\n')
edit(p, '        else if (request.action == LX_HOME_COMMAND_PULSE)\n', '        else if (request.action == LX_HOME_COMMAND_PULSE || request.action >= LX_HOME_COMMAND_LIGHT_PREV)\n')
# A valódi kliens, a titkosítás és az új szolgáltatás ugyanabban a host buildben fordul.
subprocess.run(['python3', 'tests/test_loxone_protocol.py'], check=True)
print('Integrated lighting worker, visibility guard and snapshot filtering.')
