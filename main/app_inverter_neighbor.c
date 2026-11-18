/* Az IPv4-szomszédtáblát csak a lwIP saját szála érinti. Ugyanott fut az
 * ARP-fogadás is, így a megfigyelő nem tart pbufot és nem igényel mutexet. */
#include "app_inverter_identity.h"
#include "app_wifi.h"
#include "esp_netif.h"
#include "lwip/etharp.h"
#include "lwip/prot/etharp.h"
#include "lwip/prot/iana.h"
#include "netif/ethernet.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include <stdio.h>
#include <string.h>

static struct {
    struct netif *netif;
    uint8_t mac[6];
    uint32_t address, seen_ms, sequence;
} watch;

void __real_etharp_input(struct pbuf *p, struct netif *netif);

void __wrap_etharp_input(struct pbuf *p, struct netif *netif)
{
    /* Az ethernet_input itt már levette az Ethernet-fejlécet. Szétdarabolt
     * vagy rövid fejlécet a változatlan eredeti vevő dolgozhat fel. */
    if (watch.netif && watch.netif == netif && p && p->len >= 28 && p->payload) {
        const uint8_t *arp = p->payload;
        if (arp[0] == 0 && arp[1] == 1 && arp[2] == 0x08 && arp[3] == 0 &&
            arp[4] == 6 && arp[5] == 4 && arp[6] == 0 && (arp[7] == 1 || arp[7] == 2) &&
            !memcmp(arp + 8,watch.mac,6)) {
            uint32_t sender;
            memcpy(&sender,arp + 14,4);
            if (sender) {
                watch.address = sender;
                watch.seen_ms = sys_now();
                ++watch.sequence;
            }
        }
    }
    __real_etharp_input(p,netif);
}

typedef struct {
    app_inverter_link_t *out;
    const uint8_t *mac;
    uint32_t address;
    uint8_t index;
    bool probe, ok;
} lookup_t;

static void lookup(void *arg)
{
    lookup_t *r = arg;
    if (!r) {
        memset(&watch,0,sizeof(watch));
        return;
    }
    struct netif *n = netif_get_by_index(r->index);
    if (!n || !netif_is_up(n) || !netif_is_link_up(n) || !(n->flags & NETIF_FLAG_ETHARP)) return;
    app_inverter_link_t *out = r->out;
    out->local = ip4_addr_get_u32(netif_ip4_addr(n));
    out->mask = ip4_addr_get_u32(netif_ip4_netmask(n));
    out->gateway = ip4_addr_get_u32(netif_ip4_gw(n));
    if (!out->local || !out->mask) return;
    if (!r->mac) {
        memset(&watch,0,sizeof(watch));
    } else {
        if (watch.netif != n || memcmp(watch.mac,r->mac,6)) {
            watch.netif = n;
            memcpy(watch.mac,r->mac,6);
            watch.address = 0;
        }
        out->sequence = watch.sequence;
        if ((uint32_t)(sys_now() - watch.seen_ms) < 90000) out->announced = watch.address;
    }
    bool found = false, have_gateway = false;
    for (size_t i = 0; i < ARP_TABLE_SIZE; ++i) {
        ip4_addr_t *address;
        struct netif *owner;
        struct eth_addr *mac;
        if (!etharp_get_entry(i,&address,&owner,&mac) || owner != n) continue;
        uint32_t ip = ip4_addr_get_u32(address);
        if (ip == r->address) {
            memcpy(out->exact_mac,mac->addr,6);
            found = true;
        }
        if (ip == out->gateway) {
            memcpy(out->router_mac,mac->addr,6);
            have_gateway = true;
        }
        if (r->mac && ip != out->gateway && !memcmp(r->mac,mac->addr,6) &&
            (!out->match || ip == r->address)) out->match = ip;
    }
    /* Egyetlen ismert vagy a párosított eszköz által bejelentett IP-re
     * kérünk normál ARP-feloldást. A távoli címekhez a szokásos router kell. */
    uint32_t bits = r->address & ~out->mask;
    if (r->probe && (r->address & out->mask) == (out->local & out->mask) &&
        r->address != out->local && bits && bits != ~out->mask && lwip_ntohl(r->address) < 0xe0000000) {
        if (!found) {
            ip4_addr_t wanted = {.addr=r->address};
            (void)etharp_request(n,&wanted);
        }
        if (out->gateway && !have_gateway && r->address != out->gateway)
            (void)etharp_request(n,netif_ip4_gw(n));
    }
    r->ok = true;
}

bool app_inverter_link(uint32_t address, const uint8_t mac[6], bool probe, app_inverter_link_t *out)
{
    memset(out,0,sizeof(*out));
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    int index = sta ? esp_netif_get_netif_impl_index(sta) : 0;
    if (index < 1 || index > UINT8_MAX) return false;
    snprintf(out->ssid,sizeof(out->ssid),"%s",app_wifi_configured_ssid());
    lookup_t request = {.out=out,.mac=mac,.address=address,.index=(uint8_t)index,.probe=probe};
    return tcpip_callback_wait(lookup,&request) == ERR_OK && request.ok;
}

void app_inverter_link_idle(void) { (void)tcpip_callback_wait(lookup,NULL); }

/* A változó cél-IP minden próbánál ugyanahhoz a párosított Ethernet MAC-hez
 * megy. A küldés sem ARP-bejegyzést, sem kapcsolatot nem hoz létre. */
typedef struct {
    const app_inverter_pin_t *pin;
    uint32_t target;
    bool sent;
} unicast_probe_t;

static void ask_paired_interface(void *arg)
{
    unicast_probe_t *q = arg;
    struct netif *n = watch.netif;
    const app_inverter_pin_t *pin = q->pin;
    if (!n || !netif_is_up(n) || !netif_is_link_up(n) || !(n->flags & NETIF_FLAG_ETHARP) ||
        n->hwaddr_len != ETH_HWADDR_LEN || memcmp(watch.mac,pin->mac,6) ||
        strcmp(pin->ssid,app_wifi_configured_ssid())) return;
    uint32_t own = ip4_addr_get_u32(netif_ip4_addr(n));
    uint32_t mask = ip4_addr_get_u32(netif_ip4_netmask(n));
    uint32_t gateway = ip4_addr_get_u32(netif_ip4_gw(n));
    uint32_t host = q->target & ~mask;
    if (!own || !mask || (own & mask) != pin->network || mask != pin->mask || gateway != pin->gateway ||
        (q->target & mask) != pin->network || !host || host == ~mask ||
        q->target == own || q->target == gateway) return;
    struct pbuf *packet = pbuf_alloc(PBUF_LINK,SIZEOF_ETHARP_HDR,PBUF_RAM);
    if (!packet) return;
    if (packet->len >= SIZEOF_ETHARP_HDR) {
        struct eth_addr source, destination;
        memcpy(source.addr,n->hwaddr,6);
        memcpy(destination.addr,pin->mac,6);
        struct etharp_hdr *hdr = packet->payload;
        memset(hdr,0,sizeof(*hdr));
        hdr->hwtype = PP_HTONS(LWIP_IANA_HWTYPE_ETHERNET);
        hdr->proto = PP_HTONS(ETHTYPE_IP);
        hdr->hwlen = ETH_HWADDR_LEN;
        hdr->protolen = sizeof(ip4_addr_t);
        hdr->opcode = PP_HTONS(ARP_REQUEST);
        memcpy(&hdr->shwaddr,&source,sizeof(source));
        memcpy(&hdr->sipaddr,&own,sizeof(own));
        memcpy(&hdr->dipaddr,&q->target,sizeof(q->target));
        q->sent = ethernet_output(n,packet,&source,&destination,ETHTYPE_ARP) == ERR_OK;
    }
    pbuf_free(packet);
}

bool app_inverter_link_who_has(const app_inverter_pin_t *pin, uint32_t target)
{
    unicast_probe_t request = {.pin=pin,.target=target};
    return tcpip_callback_wait(ask_paired_interface,&request) == ERR_OK && request.sent;
}
