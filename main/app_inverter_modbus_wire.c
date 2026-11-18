/* Modbus TCP, nyers RTU/TCP és SOLARMAN V5 helyi, csak olvasható adatcsatorna. */
#include "app_inverter_modbus_wire.h"
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool ready(inv_mb_t *bus, bool write)
{
    for (;;) {
        int64_t left = bus->deadline_ms - now_ms();
        if (left <= 0 || bus->fd < 0 || bus->fd >= FD_SETSIZE) return false;
        struct timeval timeout = {.tv_sec = left / 1000, .tv_usec = left % 1000 * 1000};
        fd_set set;
        FD_ZERO(&set);
        FD_SET(bus->fd, &set);
        int n = select(bus->fd + 1, write ? NULL : &set, write ? &set : NULL, NULL, &timeout);
        if (n >= 0 || errno != EINTR) return n > 0;
    }
}

void inv_mb_close(inv_mb_t *bus)
{
    if (bus->fd >= 0) close(bus->fd);
    bus->fd = -1;
}

bool inv_mb_open(inv_mb_t *bus, const char *host, uint16_t port)
{
    char service[6];
    snprintf(service, sizeof(service), "%u", (unsigned)port);
    struct addrinfo hint = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM};
    struct addrinfo *addresses = NULL;
    bus->fd = -1;
    bus->deadline_ms = now_ms() + 4400;
    if (getaddrinfo(host, service, &hint, &addresses) != 0) return false;
    for (struct addrinfo *a = addresses; a && now_ms() < bus->deadline_ms; a = a->ai_next) {
        bus->fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (bus->fd < 0) continue;
        int flags = fcntl(bus->fd, F_GETFL, 0);
        if (flags >= 0 && fcntl(bus->fd, F_SETFL, flags | O_NONBLOCK) == 0) {
            if (connect(bus->fd, a->ai_addr, a->ai_addrlen) == 0) break;
            if (errno == EINPROGRESS && ready(bus, true)) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (getsockopt(bus->fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && !err) break;
            }
        }
        inv_mb_close(bus);
    }
    freeaddrinfo(addresses);
    return bus->fd >= 0;
}

static bool transfer(inv_mb_t *bus, uint8_t *data, size_t size, bool write)
{
    while (size && ready(bus, write)) {
        ssize_t n = write ? send(bus->fd, data, size, 0) : recv(bus->fd, data, size, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) break;
        data += n;
        size -= (size_t)n;
    }
    return size == 0;
}

static uint16_t crc16(const uint8_t *bytes, size_t n)
{
    uint16_t crc = 0xffff;
    while (n--) {
        crc ^= *bytes++;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}

static uint8_t checksum(const uint8_t *bytes, size_t n)
{
    uint8_t sum = 0;
    while (n--) sum += *bytes++;
    return sum;
}

static uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void put_be16(uint8_t *p, unsigned v)
{
    p[0] = v >> 8;
    p[1] = v;
}

static size_t request(inv_mb_t *bus, uint8_t *frame, uint8_t fn, uint16_t addr, unsigned count)
{
    size_t pos = bus->frame == INV_MB_TCP ? 6 : bus->frame == INV_MB_V5 ? 26 : 0;
    uint8_t *rtu = frame + pos;
    rtu[0] = bus->unit;
    rtu[1] = fn;
    put_be16(rtu + 2, addr);
    put_be16(rtu + 4, count);
    if (bus->frame == INV_MB_TCP) {
        put_be16(frame, bus->sequence);
        put_be16(frame + 4, 6);
        return 12;
    }
    uint16_t crc = crc16(rtu, 6);
    rtu[6] = crc;
    rtu[7] = crc >> 8;
    if (bus->frame == INV_MB_RTU) return 8;
    frame[0] = 0xa5;
    frame[1] = 23;
    frame[3] = 0x10;
    frame[4] = 0x45;
    frame[5] = bus->sequence;
    for (unsigned i = 0; i < 4; ++i) frame[7 + i] = bus->serial >> (8 * i);
    frame[11] = 2;
    frame[34] = checksum(frame + 1, 33);
    frame[35] = 0x15;
    return 36;
}

static bool receive_v5(inv_mb_t *bus, uint8_t *frame, size_t capacity, size_t *length)
{
    for (unsigned i = 0; i < 4; ++i) {
        if (!transfer(bus, frame, 11, false) || frame[0] != 0xa5) return false;
        size_t total = 13U + frame[1] + ((unsigned)frame[2] << 8);
        if (total < 14 || total > capacity || !transfer(bus, frame + 11, total - 11, false))
            return false;
        if (frame[total - 1] != 0x15 || frame[total - 2] != checksum(frame + 1, total - 3))
            return false;
        for (unsigned j = 0; j < 4; ++j)
            if (frame[7 + j] != (uint8_t)(bus->serial >> (8 * j))) return false;
        /* A stick időnként állapotkeretet küld a keresett válasz előtt. */
        if (frame[4] == 0x47 || frame[4] == 0x41 || frame[4] == 0x42 ||
            frame[4] == 0x43 || frame[4] == 0x48) continue;
        if (total < 32 || frame[3] != 0x10 || frame[4] != 0x15 ||
            frame[5] != (uint8_t)bus->sequence || frame[11] != 2) return false;
        *length = total - 27;
        memmove(frame, frame + 25, *length);
        return true;
    }
    return false;
}

bool inv_mb_read(inv_mb_t *bus, uint8_t fn, uint16_t addr, unsigned count, uint16_t *words)
{
    if (bus->fd < 0 || (fn != 3 && fn != 4) || !words || !count || count > 125) return false;
    uint8_t data[288] = {0};
    ++bus->sequence;
    size_t n = request(bus, data, fn, addr, count);
    if (!transfer(bus, data, n, true)) goto broken;
    if (bus->frame == INV_MB_TCP) {
        if (!transfer(bus, data, 6, false) || be16(data) != bus->sequence || be16(data + 2))
            goto broken;
        n = be16(data + 4);
        if (n < 3 || n > 254 || !transfer(bus, data, n, false)) goto broken;
    } else {
        if (bus->frame == INV_MB_V5) {
            if (!receive_v5(bus, data, sizeof(data), &n)) goto broken;
        } else {
            if (!transfer(bus, data, 3, false)) goto broken;
            n = data[1] & 0x80 ? 5 : (size_t)data[2] + 5;
            if (!transfer(bus, data + 3, n - 3, false)) goto broken;
        }
        size_t expected = (data[1] & 0x80) ? 5 : (size_t)data[2] + 5;
        if (bus->frame == INV_MB_V5 && n == expected + 2 && !data[n - 1] && !data[n - 2])
            n -= 2;
        if (n != expected || crc16(data, n) != 0) goto broken;
        n -= 2;
    }
    if (data[0] != bus->unit) goto broken;
    /* A szabályos kivétel egy opcionális regiszter hiányát is jelezheti. */
    if (data[1] == (fn | 0x80) && n == 3) return false;
    if (data[1] != fn || data[2] != count * 2 || n != 3 + count * 2) goto broken;
    for (unsigned i = 0; i < count; ++i) words[i] = be16(data + 3 + 2 * i);
    return true;
broken:
    inv_mb_close(bus);
    return false;
}
