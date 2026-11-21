#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { INV_MB_TCP, INV_MB_RTU, INV_MB_V5, INV_MB_LSE3, INV_MB_LSE3_CRC_LENGTH } inv_mb_frame_t;

typedef struct {
    int fd;
    int64_t deadline_ms;
    inv_mb_frame_t frame;
    uint32_t serial;
    uint16_t sequence;
    uint8_t unit;
} inv_mb_t;

/* Egy mintához közös határidő és kapcsolat tartozik. Kizárólag olvasó funkciók. */
bool inv_mb_open(inv_mb_t *bus, const char *host, uint16_t port);
/* LSE-3 keretváltozat újrapróbálása ugyanazon az eredeti időkereten belül. */
bool inv_mb_open_until(inv_mb_t *bus, const char *host, uint16_t port, int64_t deadline_ms,
                       uint32_t source_ipv4);
bool inv_mb_read(inv_mb_t *bus, uint8_t function, uint16_t address, unsigned count, uint16_t *words);
void inv_mb_close(inv_mb_t *bus);
