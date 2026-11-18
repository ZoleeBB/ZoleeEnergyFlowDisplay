/* A Master csak a kézzel beállított cím első érvényes alkalmazásválasza után
 * jegyzi meg az adatgyűjtő MAC-címét. DHCP-költözéskor ugyanezt a szomszédot
 * ellenőrzi a protokollhívás előtt és után. */
#include "app_inverter_identity.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static const char wait_neighbor[] = "A helyi inverter hálózati címének azonosítására vár";
static const char mismatch[] = "A mentett IP-címen más eszköz válaszol; a párosított inverterre vár";
static const char storage[] = "A hálózati eszközazonosság nem menthető vagy nem olvasható";

static bool real_mac(const uint8_t mac[6])
{
    static const uint8_t zero[6] = {0};
    return !(mac[0] & 1) && memcmp(mac, zero, 6) != 0;
}

static bool on_link(uint32_t ip, const app_inverter_link_t *link)
{
    uint32_t host = ntohl(ip);
    return link->mask && ip && ip != link->local && ip != link->gateway &&
           host < 0xe0000000 && (host >> 24) != 127 &&
           (ip & link->mask) == (link->local & link->mask) &&
           (ip & ~link->mask) && (ip & ~link->mask) != ~link->mask;
}

static bool same_scope(const app_inverter_pin_t *pin, const app_inverter_link_t *link)
{
    return pin->network == (link->local & link->mask) && pin->mask == link->mask &&
           pin->gateway == link->gateway && !strcmp(pin->ssid,link->ssid);
}

esp_err_t app_inverter_identity_forget(nvs_handle_t h)
{
    esp_err_t err = nvs_erase_key(h,"lan_pin");
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

void app_inverter_identity_load(app_inverter_identity_t *s, const app_inverter_config_t *cfg, bool local)
{
    memset(s,0,sizeof(*s));
    app_inverter_link_idle();
    uint32_t ip;
    if (!local || inet_pton(AF_INET,cfg->host,&ip) != 1) return;
    s->pin.original = ip;
    s->pin.version = 1;
    s->pin.port = cfg->port;
    s->pin.unit = cfg->unit;
    snprintf(s->pin.profile,sizeof(s->pin.profile),"%s",cfg->profile);
    nvs_handle_t h;
    esp_err_t err = nvs_open("inverter_v4",NVS_READONLY,&h);
    if (err == ESP_OK) {
        app_inverter_pin_t saved;
        size_t size = sizeof(saved);
        err = nvs_get_blob(h,"lan_pin",&saved,&size);
        nvs_close(h);
        if (err == ESP_OK) {
            if (size != sizeof(saved) || saved.version != 1 || !real_mac(saved.mac) ||
                !memchr(saved.profile,0,sizeof(saved.profile)) || !memchr(saved.ssid,0,sizeof(saved.ssid))) {
                s->storage_error = true;
                return;
            }
            if (saved.original == ip && saved.port == cfg->port && saved.unit == cfg->unit &&
                !strcmp(saved.profile,cfg->profile)) {
                s->pin = saved;
                s->bound = true;
            }
        }
    }
    s->storage_error = err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND;
}

const char *app_inverter_identity_prepare(app_inverter_identity_t *s, char out[16])
{
    out[0] = 0;
    s->direct = false;
    if (s->storage_error) return storage;
    if (!s->pin.original) return NULL;
    uint32_t target = s->bound ? s->pin.last : s->pin.original;
    app_inverter_link_t link;
    if (!app_inverter_link(target,s->bound ? s->pin.mac : NULL,true,&link)) return wait_neighbor;
    if (s->bound && !same_scope(&s->pin,&link))
        return "Másik helyi hálózat: a mentett inverter címe nem igazolható";
    if (!on_link(target,&link)) {
        if (s->bound) return wait_neighbor;
        return NULL; /* Útválasztott címen az inverter saját MAC-címe nem látható. */
    }
    if (s->bound) {
        uint32_t candidate = link.announced;
        if (!candidate || (candidate == target && memcmp(s->pin.mac,link.exact_mac,6)))
            candidate = link.match;
        if (candidate && candidate != target && on_link(candidate,&link)) {
            target = candidate;
            if (!app_inverter_link(target,s->pin.mac,true,&link) || !same_scope(&s->pin,&link))
                return wait_neighbor;
        }
        if (!real_mac(link.exact_mac)) return wait_neighbor;
        if (memcmp(s->pin.mac,link.exact_mac,6)) return mismatch;
    } else if (!real_mac(link.exact_mac)) return wait_neighbor;
    if (link.gateway && (!real_mac(link.router_mac) || !memcmp(link.router_mac,link.exact_mac,6))) {
        /* Proxy ARP esetén a router összes továbbított eszközéhez ugyanaz a
         * MAC tartozhat: kizárólag az eredetileg megadott IP használható. */
        if (s->bound) return wait_neighbor;
        return NULL;
    }
    if (!inet_ntop(AF_INET,&target,out,16)) return wait_neighbor;
    s->chosen = target;
    memcpy(s->before,link.exact_mac,6);
    if (!s->bound) {
        s->pin.network = link.local & link.mask;
        s->pin.mask = link.mask;
        s->pin.gateway = link.gateway;
        memcpy(s->pin.ssid,link.ssid,sizeof(s->pin.ssid));
    }
    s->direct = true;
    return NULL;
}

const char *app_inverter_identity_verify(app_inverter_identity_t *s)
{
    if (!s->direct) return NULL;
    app_inverter_link_t link;
    if (!app_inverter_link(s->chosen,s->before,false,&link) || !same_scope(&s->pin,&link) ||
        !on_link(s->chosen,&link) || memcmp(s->before,link.exact_mac,6) ||
        !memcmp(s->before,link.router_mac,6)) return wait_neighbor;
    return NULL;
}

const char *app_inverter_identity_commit(app_inverter_identity_t *s)
{
    if (!s->direct) return NULL;
    if (!s->bound || s->pin.last != s->chosen) {
        app_inverter_pin_t next = s->pin;
        memcpy(next.mac,s->before,6);
        next.last = s->chosen;
        nvs_handle_t h;
        esp_err_t err = nvs_open("inverter_v4",NVS_READWRITE,&h);
        if (err == ESP_OK) {
            err = nvs_set_blob(h,"lan_pin",&next,sizeof(next));
            if (err == ESP_OK) err = nvs_commit(h);
            nvs_close(h);
        }
        if (err != ESP_OK) return storage;
        s->pin = next;
        s->bound = true;
    }
    inet_ntop(AF_INET,&s->chosen,s->active,sizeof(s->active));
    return NULL;
}
