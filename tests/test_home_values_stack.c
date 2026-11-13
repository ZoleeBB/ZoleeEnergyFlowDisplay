/* A tényleges értéktár előzetes ellenőrzése nagy veremmásolat nélkül. */
#include "app_loxone_home_runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void number(uint8_t out[24], double value)
{
    uint64_t bits;
    memset(out, 0, 24);
    memcpy(&bits, &value, sizeof(bits));
    for (unsigned i = 0; i < 8; ++i)
        out[16 + i] = (uint8_t)(bits >> (8 * i));
}

int main(void)
{
    lx_home_config_t *config = calloc(1, sizeof(*config));
    lx_home_values_t *values = calloc(1, sizeof(*values));
    lx_home_values_t *before = calloc(1, sizeof(*before));
    assert(config && values && before);
    config->consumer_count = 1;
    config->card_count = config->page_count = 2;
    config->pages[0].card_count = config->pages[1].card_count = 1;
    config->pages[1].first_card = 1;
    strcpy(config->consumers[0].source.uuid, "00000000-0000-0000-0000000000000000");
    config->consumers[0].source.scale = 1;
    for (unsigned i = 0; i < 2; ++i)
    {
        config->cards[i].type = LX_HOME_CARD_VALUE;
        config->cards[i].source = config->consumers[0].source;
        values->cards[i] = (lx_home_value_t){.value = 11 * (i + 1), .valid = true};
        values->command_supported[i] = true;
    }
    values->consumers[0] = (lx_home_value_t){.value = 33, .valid = true};
    *before = *values;
    uint8_t packet[49] = {0};
    number(packet, 4);
    number(packet + 24, 7);
    /* Érvényes rekord(ok) után hibás maradék se okozzon részleges frissítést. */
    for (size_t size = 1; size <= sizeof(packet); ++size)
        if (size % 24)
        {
            assert(!lx_home_values_update_visible(values, config, 0, 2, packet, size));
            assert(!memcmp(values, before, sizeof(*values)));
        }
    assert(!lx_home_values_update_visible(values, config, 2, 2, packet, 48));
    assert(!lx_home_values_update_visible(values, config, 0, 2, NULL, 24));
    assert(!memcmp(values, before, sizeof(*values)));
    assert(lx_home_values_update_visible(values, config, 0, 2, NULL, 0));
    assert(lx_home_values_update_visible(values, config, 0, 3, NULL, 0));
    assert(!memcmp(values, before, sizeof(*values)));
    assert(lx_home_values_update_visible(values, config, 0, 2, packet, 48));
    assert(values->consumers[0].valid && values->consumers[0].value == 7);
    assert(values->cards[0].valid && values->cards[0].value == 7);
    assert(values->cards[1].value == 22 && values->command_supported[0]);
    assert(values->command_supported[1]);
    assert(lx_home_values_update(values, config, 2, packet, 48));
    assert(values->cards[1].valid && values->cards[1].value == 7);
    number(packet, 9);
    assert(lx_home_values_update_visible(values, config, -1, 2, packet, 24));
    assert(values->consumers[0].value == 9 && values->cards[0].value == 7);
    assert(values->cards[1].value == 7);
    number(packet, NAN);
    assert(lx_home_values_update_visible(values, config, 0, 2, packet, 24));
    assert(!values->consumers[0].valid && !values->cards[0].valid);
    assert(values->cards[1].valid && values->cards[1].value == 7);
    *before = *values;
    config->consumer_count = LX_HOME_MAX_CONSUMERS + 1;
    assert(!lx_home_values_update(values, config, 2, packet, 24));
    assert(!memcmp(values, before, sizeof(*values)));
    free(before);
    free(values);
    free(config);
    puts("PASS: no full-state stack copy; complete packet preflight, no partial malformed update, visible/shared values and last event wins");
    return 0;
}
