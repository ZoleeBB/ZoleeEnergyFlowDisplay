/* A két gyártói GET valódi helyi socketen fut. Az SSDP csomagot kizárólag
 * a teszt loopback-válaszadójához irányítjuk át. */
#include "app_inverter_discovery.h"
#include "esp_heap_caps.h"
#include "mdns.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void *heap_caps_realloc(void *data, size_t n, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) && n <= 12289);
    return realloc(data, n);
}

static mdns_ip_addr_t ips[3];
static mdns_result_t device, other;
static bool queried;
struct esp_netif { int unused; };
static esp_netif_t station;

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    assert(!strcmp(key, "WIFI_STA_DEF"));
    return &station;
}

esp_err_t mdns_query_ptr(const char *service, const char *proto, uint32_t timeout, size_t max, mdns_result_t **out)
{
    assert(!queried && !strcmp(service, "_Fronius-SE-Inverter") && !strcmp(proto, "_tcp") && timeout <= 1000 && max >= 2);
    static mdns_txt_item_t txt[] = {{"DeviceSerialNumber", "S123"}};
    static uint8_t sizes[] = {4};
    ips[0] = (mdns_ip_addr_t){.addr.type=6, .next=&ips[1]};
    ips[1] = (mdns_ip_addr_t){.addr.u_addr.ip4.addr=inet_addr("127.0.0.2"), .next=&ips[2]};
    ips[2] = (mdns_ip_addr_t){.addr.u_addr.ip4.addr=inet_addr("127.0.0.3")};
    other = (mdns_result_t){.service_type="_http", .proto="_tcp", .addr=ips};
    device = (mdns_result_t){.esp_netif=&station, .service_type="_fronius-se-inverter", .proto="_tcp", .addr=ips,
                             .txt_count=1, .txt=txt, .txt_value_len=sizes, .next=&other};
    *out = &device;
    queried = true;
    return ESP_OK;
}
void mdns_query_results_free(mdns_result_t *results)
{
    assert(queried && results == &device);
    queried = false;
}

static uint16_t udp_port;
ssize_t __real_sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t __wrap_sendto(int fd, const void *data, size_t length, int flags,
                      const struct sockaddr *address, socklen_t size)
{
    struct sockaddr_in target = *(const struct sockaddr_in *)address;
    assert(size == sizeof(target) && target.sin_port == htons(1900) &&
           target.sin_addr.s_addr == inet_addr("239.255.255.250") && udp_port);
    assert(memmem(data, length, "ST: upnp:rootdevice", 19));
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    target.sin_port = htons(udp_port);
    return __real_sendto(fd, data, length, flags, (struct sockaddr *)&target, sizeof(target));
}

__attribute__((visibility("default")))
size_t probe(const char *profile, int fd, uint32_t timeout, app_inverter_discovery_result_t *out,
             size_t capacity)
{
    return app_inverter_discovery_http(profile, fd, timeout, out, capacity);
}

__attribute__((visibility("default")))
size_t native(const char *profile, uint16_t port, app_inverter_discovery_result_t *out, size_t capacity)
{
    udp_port = port;
    return app_inverter_discovery_native(profile, htonl(INADDR_LOOPBACK), out, capacity);
}
