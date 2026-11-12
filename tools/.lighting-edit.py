from pathlib import Path

def edit(path, old, new, count=1):
    p = Path(path)
    text = p.read_text()
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f'{path}: expected {count} integration anchors, got {actual}: {old[:90]}')
    p.write_text(text.replace(old, new))

h = 'main/app_loxone_lighting.h'
edit(h, 'typedef struct { unsigned id; char name[LX_LIGHT_NAME_SIZE]; } lx_light_mood_t;', '''/* A zárt kártyának csak az ismert/be állapot kell; nem őriz parancscélt,
 * nevet vagy színválasztó-munkapéldányt minden egyes körhöz. */
typedef struct
{
    char state[LX_UUID_LEN];
    lx_light_type_t type;
    bool valid, on;
} lx_light_observation_t;

typedef struct { unsigned id; char name[LX_LIGHT_NAME_SIZE]; } lx_light_mood_t;''')
edit(h, '    lx_light_circuit_t *circuits;\n', '    lx_light_circuit_t *circuits;\n    lx_light_observation_t *observations;\n')
edit(h, 'void lx_light_block_free(lx_light_block_t *block);', '''/* A részletes állapottárból kompakt kártyaállapotot készít; hiba esetén
 * változatlan marad. A részletes ablak külön, igény szerinti blokkot kap. */
bool lx_light_block_minimize(lx_light_block_t *block);
void lx_light_block_free(lx_light_block_t *block);''')

p = 'main/app_loxone_lighting_model.inc'
edit(p, 'void lx_light_block_free(lx_light_block_t *block)\n', '''bool lx_light_block_minimize(lx_light_block_t *block)
{
    if (!block) return false;
    if (!block->circuits) return true;
    lx_light_observation_t *small = block->circuit_count ?
        lx_light_alloc(block->circuit_count, sizeof(*small)) : NULL;
    if (block->circuit_count && !small) return false;
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        const lx_light_circuit_t *circuit = &block->circuits[i];
        light_copy(small[i].state, sizeof(small[i].state), circuit->state);
        small[i].type = circuit->type;
        small[i].valid = circuit->actual.valid;
        small[i].on = circuit->actual.brightness > 0;
    }
    free(block->circuits);
    block->circuits = NULL;
    block->observations = small;
    return true;
}

void lx_light_block_free(lx_light_block_t *block)
''')
edit(p, '    free(block->circuits);\n    free(block);', '    free(block->circuits);\n    free(block->observations);\n    free(block);')
edit(p, '    if (!block) return;\n    for (unsigned i = 0; i < block->circuit_count; ++i)\n', '    if (!block || !block->circuits) return;\n    for (unsigned i = 0; i < block->circuit_count; ++i)\n')
edit(p, '        if (cJSON_IsObject(names))\n', '        if (block->circuits && cJSON_IsObject(names))\n')
anchor = '''    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        lx_light_circuit_t *circuit = &block->circuits[i];
        if (!numeric && circuit->lock_state[0]'''
edit(p, anchor, '''    if (block->observations)
    {
        for (unsigned i = 0; i < block->circuit_count; ++i)
        {
            lx_light_observation_t *item = &block->observations[i];
            if (!item->state[0] || strcmp(uuid, item->state)) continue;
            if (numeric && (item->type == LX_LIGHT_SWITCH || item->type == LX_LIGHT_DIMMER))
            {
                item->valid = isfinite(value) && value >= 0 && value <= 100 &&
                    (item->type != LX_LIGHT_SWITCH || value == 0 || value == 1);
                item->on = item->valid && value > 0;
            }
            else if (!numeric && item->type == LX_LIGHT_COLOR)
            {
                lx_light_value_t color = {0};
                item->valid = lx_light_color_parse(text, length, &color);
                item->on = item->valid && color.brightness > 0;
            }
        }
        return;
    }
    for (unsigned i = 0; i < block->circuit_count; ++i)
    {
        lx_light_circuit_t *circuit = &block->circuits[i];
        if (!numeric && circuit->lock_state[0]''')
edit(p, '''        const lx_light_value_t *value = &block->circuits[i].actual;
        if (!value->valid) all_known = false;
        else if (value->brightness) any_on = true;''', '''        if (block->observations)
        {
            if (!block->observations[i].valid) all_known = false;
            else if (block->observations[i].on) any_on = true;
        }
        else
        {
            const lx_light_value_t *value = &block->circuits[i].actual;
            if (!value->valid) all_known = false;
            else if (value->brightness) any_on = true;
        }''')
edit(p, '    if (!light_allowed(block) || index >= block->circuit_count || !value || !out ||', '    if (!light_allowed(block) || !block->circuits || index >= block->circuit_count || !value || !out ||')
edit(p, '    if (!block || index >= block->circuit_count || now < 0) return false;', '    if (!block || !block->circuits || index >= block->circuit_count || now < 0) return false;')
edit(p, '    if (!block || index >= block->circuit_count) return;', '    if (!block || !block->circuits || index >= block->circuit_count) return;')

edit('tests/test_lighting.c', '    free(home);\n    lx_light_block_free(block);', '''    free(home);
    /* A kártyaállapot a teljes körmodell töredékét foglalja. */
    assert(sizeof(lx_light_observation_t) * 4 < sizeof(lx_light_circuit_t));
    assert(lx_light_block_minimize(block));
    assert(!block->circuits && block->observations);
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_OFF);
    number_event(block, 101, 42);
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_CUSTOM);
    event(block, 4, "[1]");
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_ONE_MOOD);
    event(block, 4, "[1,2]");
    lx_light_summary(block, &summary); assert(summary.label == LX_LIGHT_MANY_MOODS);
    assert(!lx_light_command_build(block, 0, &color, command));
    assert(!lx_light_take(block, 0, 2000000, command));
    lx_light_block_cancel(block);
    lx_light_circuit_snapshot(block, 0, 2000000, &snapshot);
    assert(!snapshot.actual.valid);
    event(block, 6, "{\\"AI1\\":\\"Zárt kártya\\"}");
    assert(lx_light_block_minimize(block));
    printf("Lighting memory: compact circuit %zu, detailed circuit %zu bytes\\n",
           sizeof(lx_light_observation_t), sizeof(lx_light_circuit_t));
    lx_light_block_free(block);''')
print('Applied compact visible-card model and sanitizer regression tests.')
