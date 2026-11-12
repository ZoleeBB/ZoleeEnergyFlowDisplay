/* A valódi katalógusfeldolgozó címhatárai; nincs lemásolt tesztimplementáció. */
#include "app_loxone_lighting.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *parent_uuid = "10000000-0000-0000-0000000000000001";

static void check_action(const char *parent, const char *key, const char *explicit_action,
                         const char *expected)
{
    cJSON *entry = cJSON_CreateObject();
    cJSON *control = cJSON_CreateObject();
    cJSON *children = cJSON_CreateObject();
    cJSON *child = cJSON_CreateObject();
    assert(entry && control && children && child);
    assert(cJSON_AddStringToObject(control, "type", "LightControllerV2"));
    assert(cJSON_AddStringToObject(control, "uuidAction", parent));
    assert(cJSON_AddStringToObject(child, "type", "Switch"));
    assert(cJSON_AddStringToObject(child, "name", "Határérték"));
    if (explicit_action)
        assert(cJSON_AddStringToObject(child, "uuidAction", explicit_action));
    assert(cJSON_AddItemToObject(children, key, child));
    assert(cJSON_AddItemToObject(control, "subControls", children));
    assert(lx_light_catalog_attach(entry, control, false));
    lx_light_block_t *block = lx_light_block_create(entry);
    assert(block && block->circuit_count == 1 && block->circuits);
    assert(!strcmp(block->circuits[0].action, expected));
    if (!expected[0])
        assert(block->circuits[0].blocked);
    lx_light_block_free(block);
    cJSON_Delete(control);
    cJSON_Delete(entry);
}

int main(void)
{
    check_action(parent_uuid, "AI1", NULL, "10000000-0000-0000-0000000000000001/AI1");
    check_action(parent_uuid, parent_uuid, NULL, parent_uuid);
    check_action(parent_uuid, "AI1", parent_uuid, parent_uuid);
    check_action(parent_uuid, "AI1", "", "");
    check_action(parent_uuid, "AI1", "not-a-command", "");
    check_action(parent_uuid, "", NULL, "");
    check_action(parent_uuid, "../AI1", NULL, "");
    check_action(parent_uuid, "A/B", NULL, "");
    check_action(parent_uuid, "A%2FB", NULL, "");
    check_action("", "AI1", NULL, "");
    char key[96], expected[LX_LIGHT_ACTION_SIZE];
    for (size_t length = 1; length < sizeof(key); ++length)
    {
        memset(key, 'A', length);
        key[length] = 0;
        expected[0] = 0;
        if (length <= 20)
        {
            size_t base = strlen(parent_uuid);
            memcpy(expected, parent_uuid, base);
            expected[base] = '/';
            memcpy(expected + base + 1, key, length + 1);
        }
        check_action(parent_uuid, key, NULL, expected);
    }
    /* A túl hosszú szülővel sem lehet részleges/csonkolt vezérlési címet készíteni. */
    memset(key, 'B', sizeof(key) - 1);
    key[sizeof(key) - 1] = 0;
    check_action(key, "AI1", NULL, "");
    puts("PASS: actual lighting action bounds, explicit precedence and no truncated commands");
    return 0;
}
