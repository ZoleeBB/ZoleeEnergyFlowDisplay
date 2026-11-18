/* A Master csak a kézzel beállított cím első érvényes alkalmazásválasza után
 * jegyzi meg az adatgyűjtő MAC-címét. DHCP-költözéskor ugyanezt a szomszédot
 * ellenőrzi a protokollhívás előtt és után. */
#include "app_inverter_identity.h"
#include <sys/socket.h>
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
    uint32_t first = ntohl(ip) >> 24;
    if (!first || first == 127 || first >= 224) {
        s->invalid_address = true;
        return;
    }
    s->pin.original = ip;
    s->pin.version = 1;
    s->pin.port = cfg->port;
    s->pin.unit = cfg->unit;
    snprintf(s->pin.profile,sizeof(s->pin.profile),"%s",cfg->profile);
    snprintf(s->pin.user,sizeof(s->pin.user),"%s",cfg->user);
    snprintf(s->pin.device,sizeof(s->pin.device),"%s",cfg->device);
    nvs_handle_t h;
    esp_err_t err = nvs_open("inverter_v4",NVS_READONLY,&h);
    if (err == ESP_OK) {
        app_inverter_pin_t saved;
        size_t size = sizeof(saved);
        err = nvs_get_blob(h,"lan_pin",&saved,&size);
        nvs_close(h);
        if (err == ESP_OK) {
            if (size != sizeof(saved) || saved.version != 1 || !real_mac(saved.mac) ||
                !memchr(saved.profile,0,sizeof(saved.profile)) || !memchr(saved.ssid,0,sizeof(saved.ssid)) ||
                !memchr(saved.user,0,sizeof(saved.user)) || !memchr(saved.device,0,sizeof(saved.device))) {
                s->storage_error = true;
                return;
            }
            if (saved.original == ip && saved.port == cfg->port && saved.unit == cfg->unit &&
                !strcmp(saved.profile,cfg->profile) && !strcmp(saved.device,cfg->device) && !strcmp(saved.user,cfg->user)) {
                s->pin = saved;
                s->bound = true;
            }
        }
    }
    s->storage_error = err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND;
}

void app_inverter_identity_outage(app_inverter_identity_t *s)
{
    if (s->bound) s->search.requested = true;
}

bool app_inverter_identity_searching(const app_inverter_identity_t *s)
{
    return s->search.running && s->search.old_checks == 2 && s->search.cursor > 0;
}

/* Előbb a korábbi IP-t kérdezzük újra. A táblában megmaradt régi MAC nem
 * bizonyít új elérhetőséget: a figyelő sorszáma csak valódi keretnél nő.
 * A saját alhálózat /22-ig vizsgálható végig, nagyobbnál a korábbi cím /24-e. */
bool app_inverter_identity_search_tick(app_inverter_identity_t *s, int64_t now)
{
    if (!s->bound || !s->search.requested || now < s->search.next) return false;
    app_inverter_link_t link;
    if (!app_inverter_link(s->pin.last,s->pin.mac,false,&link) || !same_scope(&s->pin,&link) ||
        !on_link(s->pin.last,&link)) {
        s->search.running = false;
        s->search.next = now + 5000000;
        return false;
    }
    if (!s->search.running) {
        s->search.sequence = link.sequence;
        s->search.cursor = 0;
        s->search.old_checks = 0;
        s->search.running = true;
    } else if (link.sequence != s->search.sequence && on_link(link.announced,&link)) {
        /* Egy friss keret után a rendes mérési út újra igazolja a konkrét címet. */
        s->search.running = s->search.requested = false;
        s->search.passes = 0;
        s->search.next = now + 15000000;
        return true;
    }
    if (s->search.old_checks < 2) {
        ++s->search.old_checks;
        (void)app_inverter_link_who_has(&s->pin,s->pin.last);
        s->search.next = now + 1000000;
        return false;
    }
    uint32_t size = ~ntohl(s->pin.mask);
    if (size > 1023) size = 255;
    if (!size || (size & (size + 1))) {
        s->search.running = s->search.requested = false;
        return false;
    }
    uint32_t previous = ntohl(s->pin.last);
    while (s->search.cursor < size) {
        /* A korábbi cím két szomszédja az első; utána kifelé haladunk. */
        unsigned i = ++s->search.cursor;
        uint32_t offset = (i & 1) ? (i + 1U) / 2U : 0U - i / 2U;
        uint32_t candidate = htonl((previous & ~size) | ((previous + offset) & size));
        if (!on_link(candidate,&link)) continue;
        (void)app_inverter_link_who_has(&s->pin,candidate);
        s->search.next = now + 500000;
        return false;
    }
    /* A körök közti 1, 2, majd 4 perces szünetben passzívan tovább figyelünk.
     * Későn visszatérő eszköznél sincs folyamatos hálózati terhelés. */
    s->search.running = false;
    s->search.next = now + (INT64_C(60000000) << s->search.passes);
    if (s->search.passes < 2) ++s->search.passes;
    return false;
}

const char *app_inverter_identity_prepare(app_inverter_identity_t *s, char out[16])
{
    out[0] = 0;
    s->direct = false;
    if (s->invalid_address) return "Érvényes egyedi invertercímet kell megadni";
    if (s->storage_error) return storage;
    if (!s->pin.original) return NULL;
    uint32_t target = s->bound ? s->pin.last : s->pin.original;
    app_inverter_link_t link;
    if (!app_inverter_link(target,s->bound ? s->pin.mac : NULL,true,&link)) return wait_neighbor;
    if (s->bound && !same_scope(&s->pin,&link))
        return "Másik helyi hálózat: a mentett inverter címe nem igazolható";
    if (target == link.local || ((target & link.mask) == (link.local & link.mask) &&
        (!(target & ~link.mask) || (target & ~link.mask) == ~link.mask)))
        return "A megadott IP a kijelző, a hálózat vagy a szórás címe";
    if (!on_link(target,&link)) {
        if (s->bound) return wait_neighbor;
        return NULL; /* Útválasztott címen az inverter saját MAC-címe nem látható. */
    }
    if (s->bound) {
        uint32_t candidate = link.announced;
        if (!candidate || (candidate == target && memcmp(s->pin.mac,link.exact_mac,6)))
            candidate = link.match;
        /* A keresés alatt az eredménytelen régi TCP-kapcsolat nem blokkolja
         * a következő címpróbát. Közben a passzívan felbukkant új jelölt
         * rögtön egy saját, ellenőrzött protokollolvasást kaphat. */
        if (s->search.running && link.sequence == s->search.sequence &&
            (!on_link(candidate,&link) || candidate == target || candidate == s->chosen))
            return "A párosított inverter helyi IP-címének keresése";
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
    memset(&s->search,0,sizeof(s->search));
    inet_ntop(AF_INET,&s->chosen,s->active,sizeof(s->active));
    return NULL;
}
