#include "app_inverter_settings.h"
#include "lwip/api.h"
#include "mdns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
static unsigned a, aaaa;
static uint32_t current;
static bool enabled;
bool app_inverter_settings_local_name(const char *host, size_t size)
{
    return enabled && size == strlen("gx.local") && !strncasecmp(host,"gx.local",size);
}
esp_err_t mdns_query_a(const char *host, uint32_t wait, esp_ip4_addr_t *out)
{
    ++a;
    assert(!strcasecmp(host,"gx") && wait <= 1000);
    out->addr = current;
    return current ? ESP_OK : ESP_FAIL;
}
esp_err_t mdns_query_aaaa(const char *host, uint32_t wait, esp_ip6_addr_t *out)
{
    ++aaaa;
    assert(!strcasecmp(host,"gx") && wait <= 1000);
    *out = (esp_ip6_addr_t){.addr={0xfe800000,0,0,1},.zone=3};
    return ESP_OK;
}
int main(void)
{
    ip_addr_t ip = {0}; err_t err = -1;
    assert(!lwip_hook_netconn_external_resolve("gx.local",&ip,NETCONN_DNS_IPV4,&err));
    enabled = true;
    assert(!lwip_hook_netconn_external_resolve("gx.home.arpa",&ip,NETCONN_DNS_IPV4,&err));
    assert(!lwip_hook_netconn_external_resolve("192.0.2.10",&ip,NETCONN_DNS_IPV4,&err));
    assert(!lwip_hook_netconn_external_resolve("other.local",&ip,NETCONN_DNS_IPV4,&err));
    assert(!a && !aaaa);
    current = 1;
    assert(lwip_hook_netconn_external_resolve("GX.LOCAL.",&ip,NETCONN_DNS_IPV4,&err) && err == ERR_OK && ip.v4 == 1);
    current = 0;
    assert(lwip_hook_netconn_external_resolve("gx.local",&ip,NETCONN_DNS_IPV4,&err) && err == ERR_VAL);
    current = 2;
    assert(lwip_hook_netconn_external_resolve("gx.local",&ip,NETCONN_DNS_IPV4,&err) && err == ERR_OK && ip.v4 == 2);
    assert(a == 3 && !aaaa);
    current = 0;
    assert(lwip_hook_netconn_external_resolve("gx.local",&ip,NETCONN_DNS_IPV4_IPV6,&err) && err == ERR_OK && ip.v6.zone == 3);
    assert(a == 4 && aaaa == 1);
    puts("PASS: inverter-only fresh mDNS lookup, lease change, failed/IPv6 fallback and ordinary DNS passthrough");
}
