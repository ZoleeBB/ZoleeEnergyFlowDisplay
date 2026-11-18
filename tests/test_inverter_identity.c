/* A foglalt, újraosztott, bejelentett és tápvesztés után visszaolvasott IP
 * ugyanazon a modellen halad át, mint az ESP-IDF mérési útjában. */
#include "app_inverter_identity.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static app_inverter_pin_t stored;
static bool saved, failed;
static const uint8_t inverter[6] = {0x02,1,2,3,4,5};
static const uint8_t other[6] = {0x02,2,3,4,5,6};
static const uint8_t router[6] = {0x02,3,4,5,6,7};
static uint32_t old_ip, new_ip, announced, found;
static const uint8_t *old_mac, *new_mac;
static const char *ssid = "otthon", *mask = "255.255.255.0";
static bool expect_last_block;
static unsigned writes, probes, sequence, probe_seen[256];
static uint32_t responds, last_probed;
bool app_inverter_link_who_has(const app_inverter_pin_t *pin,uint32_t target)
{
    assert(!memcmp(pin->mac,inverter,6));
    if (expect_last_block) assert((ntohl(target) >> 8) == (ntohl(pin->last) >> 8));
    ++probes; last_probed = target;
    ++probe_seen[ntohl(target) & 255];
    if (target == responds) {
        announced = found = target;
        ++sequence;
    }
    return true;
}

void app_inverter_link_idle(void) {}
bool app_inverter_link(uint32_t address,const uint8_t mac[6],bool probe,app_inverter_link_t *link)
{
    (void)probe;
    *link = (app_inverter_link_t){.local=inet_addr("192.168.1.5"),.mask=inet_addr(mask),
        .gateway=inet_addr("192.168.1.1")};
    strcpy(link->ssid,ssid);
    memcpy(link->router_mac,router,6);
    const uint8_t *exact = address == old_ip ? old_mac : address == new_ip ? new_mac : NULL;
    if (exact) memcpy(link->exact_mac,exact,6);
    if (mac && !memcmp(mac,inverter,6)) {
        link->announced = announced;
        link->match = found; link->sequence=sequence;
    }
    return true;
}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) {
    (void)mode; assert(!strcmp(name,"inverter_v4")); *h=1; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { assert(h==1); }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t len) {
    assert(h==1 && !strcmp(key,"lan_pin") && len==sizeof(stored));
    if (failed) return ESP_FAIL;
    memcpy(&stored,data,len); saved=true; writes++; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *len) {
    assert(h==1 && !strcmp(key,"lan_pin") && *len>=sizeof(stored));
    if (!saved) return ESP_ERR_NVS_NOT_FOUND;
    memcpy(data,&stored,sizeof(stored)); *len=sizeof(stored); return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key) {
    assert(h==1 && !strcmp(key,"lan_pin")); saved=false; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h==1); return ESP_OK; }

int main(void)
{
    app_inverter_config_t cfg = {.profile="sungrow",.host="192.168.1.20",.port=502,.unit=1};
    app_inverter_identity_t s;
    char actual[16];
    old_ip=inet_addr(cfg.host); new_ip=inet_addr("192.168.1.42");
    app_inverter_identity_load(&s,&cfg,true);
    assert(app_inverter_identity_prepare(&s,actual));
    old_mac=inverter;
    assert(!app_inverter_identity_prepare(&s,actual) && !strcmp(actual,cfg.host));
    old_mac=other;
    assert(app_inverter_identity_verify(&s) && !writes);
    old_mac=inverter;
    assert(!app_inverter_identity_prepare(&s,actual));
    assert(!app_inverter_identity_verify(&s));
    failed=true; assert(app_inverter_identity_commit(&s) && !s.bound);
    failed=false; assert(!app_inverter_identity_commit(&s) && s.bound && writes==1);
    assert(!app_inverter_identity_commit(&s) && writes==1);
    old_mac=other;
    assert(app_inverter_identity_prepare(&s,actual) && !actual[0]);
    announced=new_ip;
    assert(app_inverter_identity_prepare(&s,actual)); /* puszta bejelentés kevés */
    new_mac=inverter;
    assert(!app_inverter_identity_prepare(&s,actual) && !strcmp(actual,"192.168.1.42"));
    assert(!app_inverter_identity_verify(&s) && !app_inverter_identity_commit(&s) && writes==2);
    assert(!strcmp(s.active,"192.168.1.42") && !strcmp(cfg.host,"192.168.1.20"));
    announced=0;
    app_inverter_identity_load(&s,&cfg,true);
    assert(s.bound && !app_inverter_identity_prepare(&s,actual) && !strcmp(actual,"192.168.1.42"));
    ssid="vendeg"; assert(app_inverter_identity_prepare(&s,actual)); ssid="otthon";
    new_mac=other; old_mac=inverter; found=old_ip;
    assert(!app_inverter_identity_prepare(&s,actual) && !strcmp(actual,"192.168.1.20"));
    /* Az új címet egyetlen kész ARP-sor vagy bejelentés sem árulja el.
     * Közben egy másik eszköz elfoglalja a régi címet. */
    announced=found=0;
    responds=old_ip;
    old_mac=inverter; new_mac=other;
    app_inverter_identity_outage(&s);
    int64_t t=1000000;
    bool discovered=false;
    for (unsigned i=0;i<600 && !discovered;++i,t+=250000)
        discovered=app_inverter_identity_search_tick(&s,t);
    assert(discovered && probes>2 && last_probed==old_ip);
    assert(probe_seen[42]==2 && !probe_seen[5] && !probe_seen[1] && !probe_seen[0] && !probe_seen[255]);
    assert(!app_inverter_identity_prepare(&s,actual) && !strcmp(actual,"192.168.1.20"));
    assert(!app_inverter_identity_verify(&s) && !app_inverter_identity_commit(&s));
    assert(!s.search.requested && !app_inverter_identity_searching(&s));
    /* Ha a régi MAC azonnal válaszol a korábbi IP-re, például csak az
     * alkalmazásszintű API romlott el, egyetlen más címet sem kérdezünk. */
    unsigned before=probes;
    responds=old_ip; app_inverter_identity_outage(&s);
    assert(!app_inverter_identity_search_tick(&s,t));
    assert(app_inverter_identity_search_tick(&s,t+1000000));
    assert(probes==before+1 && last_probed==old_ip);
    responds=announced=found=0;
    app_inverter_identity_outage(&s);
    before=probes; t+=16000000;
    for (unsigned i=0;i<560;++i,t+=250000) (void)app_inverter_identity_search_tick(&s,t);
    assert(probes==before+2+251); /* 254 gépcímből a korábbi, a gateway és a panel kimarad */
    before=probes;
    for (unsigned i=0;i<100;++i,t+=250000) (void)app_inverter_identity_search_tick(&s,t);
    assert(probes==before); /* A teljes kör után a szünetben nincs újabb terhelés. */
    ssid="masik";
    (void)app_inverter_identity_search_tick(&s,t+120000000);
    assert(probes==before);
    ssid="otthon";
    assert(!app_inverter_identity_forget(1));
    app_inverter_identity_load(&s,&cfg,true);
    /* Nagy hálózaton csak a korábban használt /24 kerül sorra. Kihagyott
     * perceket az ütemező egyetlen próbával követ, nem utólagos csomagsorral. */
    mask="255.255.0.0"; old_mac=inverter;
    assert(!app_inverter_identity_prepare(&s,actual));
    assert(!app_inverter_identity_verify(&s) && !app_inverter_identity_commit(&s));
    expect_last_block=true;
    app_inverter_identity_outage(&s);
    before=probes;
    (void)app_inverter_identity_search_tick(&s,t);
    t+=900000000;
    (void)app_inverter_identity_search_tick(&s,t);
    assert(probes==before+2);
    for (unsigned i=0;i<560;++i,t+=250000) (void)app_inverter_identity_search_tick(&s,t);
    assert(probes==before+2+253); /* .0 és .255 is gépcím ezen a tényleges /16-on */
    expect_last_block=false;
    mask="255.255.255.0";
    assert(!app_inverter_identity_forget(1));
    app_inverter_identity_load(&s,&cfg,true);
    old_mac=router;
    assert(!app_inverter_identity_prepare(&s,actual) && !s.direct && !s.bound);
    strcpy(cfg.host,"10.10.0.2"); app_inverter_identity_load(&s,&cfg,true);
    assert(!app_inverter_identity_prepare(&s,actual) && !actual[0] && !s.direct);
    strcpy(cfg.host,"gx.local"); app_inverter_identity_load(&s,&cfg,true);
    assert(!app_inverter_identity_prepare(&s,actual) && !s.pin.original);
    strcpy(cfg.host,"192.168.1.20"); app_inverter_identity_load(&s,&cfg,false);
    assert(!app_inverter_identity_prepare(&s,actual) && !s.pin.original);
    puts("PASS: IPv4 pairing, duplicate IP rejection, DHCP move, ARP confirmation, persistent identity and network scope");
}
