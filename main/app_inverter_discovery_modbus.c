/* A port csak jelölt. A kiválasztott egységet a saját, csak olvasható
 * névtáblája, majd a rendes energiameghajtó egy sikeres mintája igazolja. */
#include "app_inverter_discovery.h"
#include "app_inverter_modbus_internal.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>

typedef app_inverter_discovery_result_t result_t;
typedef bool (*identity_fn)(inv_mb_t *, const char *, result_t *);

static bool starts(const char *text, const char *prefix)
{
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

/* A gyártók az ASCII-mezőket nullával vagy szóközzel töltik ki.
 * Beágyazott NUL utáni adatot és vezérlőkaraktert nem jelenítünk meg. */
static bool unpack(const uint16_t *words, size_t count, char *out, size_t capacity)
{
    if (2 * count >= capacity) return false;
    size_t length = 0;
    bool ended = false;
    out[0] = '\0';
    for (size_t i = 0; i < 2 * count; ++i) {
        unsigned ch = (words[i / 2] >> (i % 2 ? 0 : 8)) & 255;
        if (!ch) ended = true;
        if (!ch || (ended && ch == ' ')) continue;
        if (ended || ch < 32 || ch > 126) {
            out[0] = '\0';
            return false;
        }
        out[length++] = (char)ch;
    }
    while (length && out[length - 1] == ' ') --length;
    out[length] = '\0';
    return length != 0;
}

static bool identifier(const char *s, size_t min, size_t max)
{
    size_t length = strlen(s);
    if (length < min || length > max) return false;
    for (size_t i = 0; i < length; ++i)
        if (!isalnum((unsigned char)s[i])) return false;
    return true;
}

static bool huawei(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[25];
    if (!inv_mb_read(bus, 3, 30000, 25, w) || !unpack(w, 15, r->model, sizeof(r->model)))
        return false;
    size_t length = strlen(r->model);
    if (!(starts(r->model, "SUN2000-") || starts(r->model, "SUN2000L-")) ||
        (strcmp(r->model + length - 3, "-L1") && strcmp(r->model + length - 3, "-M1")))
        return false;
    unpack(w + 15, 10, r->serial, sizeof(r->serial));
    return true;
}

static bool goodwe(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[13];
    if (!inv_mb_read(bus, 3, 35003, 13, w) || !unpack(w, 8, r->serial, sizeof(r->serial)) ||
        !identifier(r->serial, 16, 16)) return false;
    if (unpack(w + 8, 5, r->model, sizeof(r->model)) && starts(r->model, "GW"))
        return strstr(r->model, "-ET") || strstr(r->model, "-EH");
    /* A korai ET generációnál a névmező üres lehet. A dokumentált, 3
     * karakteres családkódot a 16 karakteres gyári számból vesszük. */
    static const char *const codes[] = {"ETU", "ETL", "ETR", "ETT", "EHU", "EHR"};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        if (!strncmp(r->serial + 5, codes[i], 3)) {
            snprintf(r->model, sizeof(r->model), "GoodWe %.2s", codes[i]);
            return true;
        }
    }
    return false;
}

static bool sungrow(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[11];
    if (!inv_mb_read(bus, 4, 4989, 11, w)) return false;
    unsigned code = w[10];
    static const char *const ratings[] = {"5.0", "6.0", "8.0", "10"};
    const char *suffix;
    if (code >= 0xe00 && code <= 0xe03) suffix = "";
    else if (code >= 0xe08 && code <= 0xe0b) suffix = "-V122";
    else if (code >= 0xe0c && code <= 0xe0f) suffix = "-V112";
    else if (code >= 0xe10 && code <= 0xe13) suffix = "-20";
    else return false;
    snprintf(r->model, sizeof(r->model), "Sungrow SH%sRT%s", ratings[code & 3], suffix);
    unpack(w, 10, r->serial, sizeof(r->serial));
    return true;
}

static bool solax_variant(const char *serial, const char *device)
{
    if (!strcasecmp(device, "X1-G4") || !strcasecmp(device, "x1"))
        return starts(serial, "H43") || starts(serial, "H44") || starts(serial, "H450") ||
               starts(serial, "H460") || starts(serial, "H475");
    if (!strcasecmp(device, "X3-G4-PRO")) return starts(serial, "10K");
    return starts(serial, "H34") || starts(serial, "N34");
}

static bool solax(inv_mb_t *bus, const char *device, result_t *r)
{
    static const unsigned addresses[] = {0, 0x300, 0x1a10};
    uint16_t w[7];
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        if (!inv_mb_read(bus, 3, addresses[i], 7, w)) continue;
        if (addresses[i] == 0x300)
            for (size_t j = 0; j < 7; ++j) w[j] = (uint16_t)((w[j] << 8) | (w[j] >> 8));
        if (unpack(w, 7, r->serial, sizeof(r->serial)) && identifier(r->serial, 9, 14) &&
            solax_variant(r->serial, device)) {
            snprintf(r->model, sizeof(r->model), "SolaX %s", device[0] ? device : "X3-G4");
            return true;
        }
    }
    return false;
}

static bool solplanet(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[24];
    if (!inv_mb_read(bus, 4, 1002, 24, w) || !unpack(w + 16, 8, r->model, sizeof(r->model)))
        return false;
    if (strcasecmp(r->model, "ASW8000-T") && strcasecmp(r->model, "ASW 8000-T")) return false;
    unpack(w, 16, r->serial, sizeof(r->serial));
    return true;
}

static bool solaredge(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[36];
    char maker[33];
    if (!inv_mb_read(bus, 3, 40000, 36, w) || w[0] != 0x5375 || w[1] != 0x6e53 ||
        w[2] != 1 || w[3] != 65 || !unpack(w + 4, 16, maker, sizeof(maker)) ||
        strcasecmp(maker, "SolarEdge") || !unpack(w + 20, 16, r->model, sizeof(r->model)))
        return false;
    /* Másik gyártó SunSpec készüléke, illetve egy önálló mérő nem inverter. */
    if (!inv_mb_read(bus, 3, 40069, 2, w) ||
        !((w[0] >= 101 && w[0] <= 103) || (w[0] >= 111 && w[0] <= 113))) return false;
    if (inv_mb_read(bus, 3, 40052, 16, w)) unpack(w, 16, r->serial, sizeof(r->serial));
    return true;
}

static bool victron(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[6];
    if (!inv_mb_read(bus, 3, 800, 6, w) || !unpack(w, 6, r->serial, sizeof(r->serial)) ||
        strlen(r->serial) != 12 || strspn(r->serial, "0123456789abcdefABCDEF") != 12 ||
        strspn(r->serial, "0") == 12 || strspn(r->serial, "fF") == 12) return false;
    snprintf(r->model, sizeof(r->model), "Victron GX");
    return true;
}

static bool deye(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[8];
    if (!inv_mb_read(bus, 3, 0, 8, w) || (w[0] != 5 && w[0] != 0x0500) ||
        !unpack(w + 3, 5, r->serial, sizeof(r->serial)) || !identifier(r->serial, 10, 10) ||
        !inv_mb_read(bus, 3, 22, 1, w) || (w[0] & 0x0f0f) != 0x0203) return false;
    snprintf(r->model, sizeof(r->model), "Deye LV 3 fázis / 2 MPPT");
    return true;
}

static bool sofar(inv_mb_t *bus, const char *device, result_t *r)
{
    (void)device;
    uint16_t w[10];
    if (!inv_mb_read(bus, 3, 0x445, 8, w) || !unpack(w, 8, r->serial, sizeof(r->serial)))
        return false;
    /* A csak tárolót kezelő SP1ES120N6 külön regiszterkészletű. A hosszú SH
     * számot két helyen tartja az új firmware, családkódja önmagában kevés. */
    if (starts(r->serial, "SP1ES120N6")) return false;
    if (starts(r->serial, "SH1")) {
        if (!inv_mb_read(bus, 3, 0x470, 2, w + 8) ||
            !unpack(w, 10, r->serial, sizeof(r->serial)) ||
            !identifier(r->serial, 20, 20)) return false;
    } else if (!(starts(r->serial, "SP1") || starts(r->serial, "SP2")) ||
               !identifier(r->serial, 14, 16)) return false;
    snprintf(r->model, sizeof(r->model), "SOFAR HYD KTL-3PH");
    return true;
}

static identity_fn identity_reader(const char *profile)
{
    static const struct { const char *profile; identity_fn read; } profiles[] = {
        {"huawei_modbus", huawei}, {"goodwe_modbus", goodwe}, {"sungrow_modbus", sungrow},
        {"solax_modbus", solax}, {"solplanet_modbus", solplanet}, {"solaredge_modbus", solaredge},
        {"victron_modbus", victron}, {"deye_modbus", deye}, {"deye_rtu", deye},
        {"deye_v5", deye}, {"deye_lse3", deye}, {"sofar_modbus", sofar}, {"sofar_v5", sofar}
    };
    for (size_t i = 0; profile && i < sizeof(profiles) / sizeof(profiles[0]); ++i)
        if (!strcmp(profile, profiles[i].profile)) return profiles[i].read;
    return NULL;
}

bool app_inverter_discovery_modbus_supported(const char *profile)
{
    return identity_reader(profile) != NULL;
}

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

bool app_inverter_discovery_modbus(const char *profile, const char *device, uint8_t unit, int fd,
                                  uint32_t timeout_ms, result_t *out)
{
    inv_mb_t bus = {.fd = fd, .unit = unit};
    app_inverter_config_t *cfg = NULL;
    identity_fn read = identity_reader(profile);
    bool found = false;
    if (out) {
        out->model[0] = out->serial[0] = out->device[0] = '\0';
        out->verified = false;
    }
    if (!out || !read || fd < 0 || !timeout_ms) goto done;
    if (!device) device = "";
    if (strlen(device) >= sizeof(cfg->device)) goto done;
    cfg = calloc(1, sizeof(*cfg));
    if (!cfg) goto done;
    snprintf(cfg->profile, sizeof(cfg->profile), "%s", profile);
    snprintf(cfg->device, sizeof(cfg->device), "%s", device);
    cfg->unit = unit;
    uint16_t default_port;
    if (!app_inverter_modbus_prepare(cfg, &bus, &default_port)) goto done;
    bus.deadline_ms = now_ms() + timeout_ms;
    int64_t deadline = bus.deadline_ms;
    struct sockaddr_in local, peer;
    socklen_t local_len = sizeof(local), peer_len = sizeof(peer);
    bool retry_lse = bus.frame == INV_MB_LSE3 && getpeername(fd, (struct sockaddr *)&peer, &peer_len) == 0 &&
                     peer.sin_family == AF_INET && getsockname(fd, (struct sockaddr *)&local, &local_len) == 0 &&
                     local.sin_family == AF_INET && local.sin_addr.s_addr;
    if (retry_lse) bus.deadline_ms -= timeout_ms / 2;
    found = read(&bus, device, out);
    if (!found && retry_lse) {
        char host[INET_ADDRSTRLEN];
        inv_mb_close(&bus);
        bus.frame = INV_MB_LSE3_CRC_LENGTH;
        if (inet_ntop(AF_INET, &peer.sin_addr, host, sizeof(host)) &&
            inv_mb_open_until(&bus, host, ntohs(peer.sin_port), deadline, local.sin_addr.s_addr))
            found = read(&bus, device, out);
    }
    if (found) {
        bus.deadline_ms = deadline;
        app_energy_sample_t sample;
        out->verified = app_inverter_modbus_sample(cfg, &bus, &sample);
        snprintf(out->device, sizeof(out->device), "%s", device);
    }
done:
    if (!found && out) out->model[0] = out->serial[0] = '\0';
    free(cfg);
    inv_mb_close(&bus);
    return found;
}
