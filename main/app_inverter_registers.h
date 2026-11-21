#pragma once

#include <stddef.h>
#include <stdint.h>

#define APP_INVERTER_REGISTER_MAX 32

typedef struct {
    uint32_t mask;
    uint16_t address[APP_INVERTER_REGISTER_MAX];
} app_inverter_overrides_t;

typedef enum {
    INV_REG_U16, INV_REG_S16, INV_REG_U32_BE, INV_REG_U32_LE,
    INV_REG_S32_BE, INV_REG_S32_LE, INV_REG_F32_BE, INV_REG_F32_LE,
    INV_REG_TEXT, INV_REG_BASE,
} app_inverter_register_type_t;

typedef struct {
    const char *key, *hu, *en;
    /* NULL: abszolút PDU-cím. Egyébként a névvel jelzett modell/bank kezdetétől mért szóeltolás. */
    const char *base;
    const char *format;
    uint16_t address;
    uint8_t function, words;
    app_inverter_register_type_t type;
} app_inverter_register_t;

/* Csak flashben tárolt séma. A sorrend profilonként stabil, a kulcsok nem lokalizáltak. */
const app_inverter_register_t *app_inverter_registers(const char *profile, size_t *count);
