/* Az SDK feloldási útján marad a beállított név, így a HTTP Host, TLS SNI
 * és tanúsítványnév akkor is változatlan, ha a készülék DHCP-címe változik. */
#include "app_inverter_settings.h"
#include "lwip/api.h"
#include "mdns.h"
#include <string.h>
#include <strings.h>

static bool query_v4(const char *label, ip_addr_t *address)
{
    esp_ip4_addr_t ipv4;
    if (mdns_query_a(label, 700, &ipv4) != ESP_OK || !ipv4.addr) return false;
    ip_addr_set_ip4_u32(address, ipv4.addr);
    return true;
}

#if LWIP_IPV6
static bool query_v6(const char *label, ip_addr_t *address)
{
    esp_ip6_addr_t ipv6;
    if (mdns_query_aaaa(label, 700, &ipv6) != ESP_OK) return false;
    ip_addr_copy_from_ip6(*address, ipv6);
    return !ip_addr_isany(address);
}
#endif

/* Az ESP-IDF netconn hook a hívó feladaton fut, még az lwIP DNS-cache előtt.
 * Csak az aktív, elmentett inverter .local nevéhez küldünk célzott kérdést. */
int lwip_hook_netconn_external_resolve(const char *name, ip_addr_t *address, u8_t family, err_t *error)
{
    if (!name || !address || !error) return 0;
    size_t len = strlen(name);
    if (len && name[len - 1] == '.') --len;
    if (len <= 6 || len > 69 || strncasecmp(name + len - 6, ".local", 6) ||
        memchr(name, '.', len - 6) || !app_inverter_settings_local_name(name, len)) return 0;
    char label[64];
    memcpy(label, name, len - 6);
    label[len - 6] = 0;
    bool ok;
#if LWIP_IPV6
    if (family == NETCONN_DNS_IPV6 || family == NETCONN_DNS_IPV6_IPV4) {
        ok = query_v6(label, address);
        if (!ok && family == NETCONN_DNS_IPV6_IPV4) ok = query_v4(label, address);
    } else {
        ok = query_v4(label, address);
        if (!ok && family == NETCONN_DNS_IPV4_IPV6) ok = query_v6(label, address);
    }
#else
    (void)family;
    ok = query_v4(label, address);
#endif
    *error = ok ? ERR_OK : ERR_VAL;
    return 1;
}
