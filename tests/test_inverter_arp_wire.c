/* Valódi esp-lwIP Ethernet-/ARP-feldolgozó, szimulált hálózati driverrel. */
#include "app_inverter_identity.h"
#include "esp_netif.h"
#include "netif/ethernet.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static struct netif sta, inverter;
static bool cable_connected;
static unsigned requests, replies;
static unsigned frees;
static const uint8_t own[6] = {2,1,1,1,1,1};
static const uint8_t peer[6] = {2,2,2,2,2,2};
static const uint8_t replacement[6] = {2,3,3,3,3,3};
const char *app_wifi_configured_ssid(void) { return "home"; }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(!strcmp(key,"WIFI_STA_DEF")); return (esp_netif_t *)&sta;
}
int esp_netif_get_netif_impl_index(esp_netif_t *netif) { (void)netif; return 1; }
struct netif *netif_get_by_index(u8_t index) { return index==1 ? &sta : NULL; }
u32_t sys_now(void) { return 5000; }
err_t tcpip_callback_wait(tcpip_callback_fn f,void *ctx) { f(ctx); return ERR_OK; }
u16_t lwip_htons(u16_t value) { return __builtin_bswap16(value); }
u32_t lwip_htonl(u32_t value) { return __builtin_bswap32(value); }
u8_t ip4_addr_isbroadcast_u32(u32_t ip,const struct netif *n) {
    return ip==0xffffffff || ip==(netif_ip4_addr(n)->addr|~netif_ip4_netmask(n)->addr);
}
u8_t pbuf_free(struct pbuf *p)
{
    assert(p && p->ref==1);
    p->ref=0; frees++;
    if (p->type_internal == (u8_t)PBUF_RAM) free(p);
    return 1;
}
struct pbuf *pbuf_alloc(pbuf_layer layer,u16_t len,pbuf_type type) {
    if (!cable_connected) return NULL;
    assert(layer == PBUF_LINK && type == PBUF_RAM);
    struct pbuf *p = calloc(1,sizeof(*p) + 14 + len);
    assert(p);
    p->payload=(uint8_t *)(p+1)+14; p->len=p->tot_len=len;
    p->ref=1; p->type_internal=(u8_t)PBUF_RAM;
    return p;
}
struct pbuf *pbuf_clone(pbuf_layer layer,pbuf_type type,struct pbuf *p) {
    (void)layer; (void)type; (void)p; return NULL;
}
void pbuf_ref(struct pbuf *p) { p->ref++; }
u8_t pbuf_add_header(struct pbuf *p,size_t size)
{
    assert(p->type_internal==(u8_t)PBUF_RAM && size==14 && p->payload==(uint8_t *)(p+1)+size);
    p->payload=(uint8_t *)p->payload-size; p->len+=size; p->tot_len+=size; return 0;
}
u8_t pbuf_remove_header(struct pbuf *p,size_t len) {
    if (len>p->len) return 1;
    p->payload=(uint8_t *)p->payload+len; p->len-=len; p->tot_len-=len; return 0;
}
err_t ip4_input(struct pbuf *p,struct netif *n) { (void)n; pbuf_free(p); return ERR_OK; }

/* A 14 bájtos Ethernet-fejléc eltávolítását is az eredeti ethernet_input végzi. */
static void frame(const uint8_t mac[6],unsigned sender,unsigned target,unsigned opcode,unsigned size)
{
    uint8_t bytes[42] = {0};
    memcpy(bytes,own,6); memcpy(bytes+6,mac,6); bytes[12]=8; bytes[13]=6;
    uint8_t *arp=bytes+14;
    arp[1]=1; arp[2]=8; arp[4]=6; arp[5]=4; arp[7]=opcode;
    memcpy(arp+8,mac,6); arp[14]=192; arp[15]=168; arp[16]=1; arp[17]=sender;
    memcpy(arp+18,own,6); arp[24]=192; arp[25]=168; arp[26]=1; arp[27]=target;
    struct pbuf p = {.payload=bytes,.len=size,.tot_len=size,.ref=1};
    unsigned before=frees;
    assert(ethernet_input(&p,&sta)==ERR_OK && frees==before+1);
}
/* Kizárólag a fizikai vezetéket utánozzuk. Mindkét félen az eredeti
 * ethernet_input és etharp_input dolgozza fel a másik fél teljes keretét. */
static err_t deliver(struct netif *from,struct pbuf *p)
{
    bool request=from==&sta;
    struct netif *to=request ? &inverter : &sta;
    uint8_t bytes[42];
    assert(!p->next && p->len==sizeof(bytes));
    memcpy(bytes,p->payload,sizeof(bytes));
    assert(!memcmp(bytes,to->hwaddr,6) && !memcmp(bytes+6,from->hwaddr,6));
    assert(bytes[12]==8 && bytes[13]==6 && bytes[20]==0 && bytes[21]==(request ? 1 : 2));
    if (request) ++requests; else ++replies;
    struct pbuf rx={.payload=bytes,.len=sizeof(bytes),.tot_len=sizeof(bytes),.ref=1};
    assert(ethernet_input(&rx,to)==ERR_OK && !rx.ref);
    return ERR_OK;
}

int main(void)
{
    sta.flags=NETIF_FLAG_UP|NETIF_FLAG_LINK_UP|NETIF_FLAG_ETHARP|NETIF_FLAG_BROADCAST;
    sta.hwaddr_len=6; memcpy(sta.hwaddr,own,6);
    IP4_ADDR(ip_2_ip4(&sta.ip_addr),192,168,1,5);
    IP4_ADDR(ip_2_ip4(&sta.netmask),255,255,255,0);
    IP4_ADDR(ip_2_ip4(&sta.gw),192,168,1,1);
    ip4_addr_t initial, moved;
    IP4_ADDR(&initial,192,168,1,20); IP4_ADDR(&moved,192,168,1,42);
    app_inverter_link_t link;
    frame(peer,42,42,1,42); /* inaktív megfigyelő nem gyűjt adatot */
    assert(app_inverter_link(initial.addr,peer,false,&link) && !link.announced);
    frame(peer,20,5,2,42);
    assert(app_inverter_link(initial.addr,peer,false,&link) && !memcmp(link.exact_mac,peer,6));
    frame(replacement,20,5,2,42);
    frame(peer,42,42,1,42); /* gratuitous ARP: a lwIP nem rögzíti az új címet a cache-be */
    assert(app_inverter_link(moved.addr,peer,false,&link) && link.announced==moved.addr);
    assert(!link.exact_mac[0]);
    frame(peer,42,5,2,42);
    assert(app_inverter_link(moved.addr,peer,false,&link) && !memcmp(link.exact_mac,peer,6));
    frame(replacement,90,90,1,42);
    frame(peer,88,88,1,23);
    frame(peer,99,99,9,42);
    assert(app_inverter_link(moved.addr,peer,false,&link) && link.announced==moved.addr);
    app_inverter_link_idle();
    frame(peer,77,77,1,42);
    assert(app_inverter_link(moved.addr,peer,false,&link) && !link.announced);
    /* Az inverter csendben újraindult .83-on. A .20 korábbi címen már más
     * MAC van. A nem megfelelő cél-IP-kre a valódi inverteroldali lwIP hallgat. */
    ip4_addr_t silent;
    IP4_ADDR(&silent,192,168,1,83);
    inverter=sta;
    inverter.num=1;
    memcpy(inverter.hwaddr,peer,6);
    ip_2_ip4(&inverter.ip_addr)->addr=silent.addr;
    sta.linkoutput=inverter.linkoutput=deliver;
    app_inverter_pin_t pin={.last=initial.addr,.network=link.local & link.mask,
        .mask=link.mask,.gateway=link.gateway,.ssid="home"};
    memcpy(pin.mac,peer,6);
    assert(app_inverter_link(silent.addr,peer,false,&link) && !link.announced && !link.exact_mac[0]);
    cable_connected=true;
    assert(!app_inverter_link_who_has(&pin,link.local) &&
           !app_inverter_link_who_has(&pin,link.gateway));
    ip4_addr_t foreign; IP4_ADDR(&foreign,192,168,2,83);
    assert(!app_inverter_link_who_has(&pin,foreign.addr));
    assert(app_inverter_link_who_has(&pin,initial.addr));
    assert(app_inverter_link_who_has(&pin,moved.addr));
    assert(requests==2 && !replies);
    assert(app_inverter_link_who_has(&pin,silent.addr));
    assert(requests==3 && replies==1);
    assert(app_inverter_link(silent.addr,peer,false,&link) && link.announced==silent.addr &&
           link.sequence!=0 && !memcmp(link.exact_mac,peer,6));
    app_inverter_link_idle();
    assert(!app_inverter_link_who_has(&pin,silent.addr) && requests==3);
    puts("PASS: linked ESP lwIP on both peers, passive announcements, silent DHCP move, "
         "MAC-only unicast requests, real ARP reply and inactive forwarding");
}
