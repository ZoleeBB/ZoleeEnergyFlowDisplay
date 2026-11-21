/* Ugyanabban a folyamatban két külön IP-re kapcsolódik a valódi Modbus kliens,
 * miközben csak a router DNS-válaszát helyettesítjük. */
#include "app_inverter_modbus_wire.h"
#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned lookups;
int __real_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
int __wrap_getaddrinfo(const char *name, const char *port, const struct addrinfo *hints, struct addrinfo **out)
{
    assert(!strcmp(name, "inverter.home.arpa"));
    ++lookups;
    if (lookups == 2) return EAI_AGAIN;
    return __real_getaddrinfo(lookups == 1 ? "127.0.0.1" : "127.0.0.2", port, hints, out);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    uint16_t port = (uint16_t)atoi(argv[1]);
    for (unsigned i = 0; i < 3; ++i) {
        inv_mb_t bus = {.fd=-1};
        bool connected = inv_mb_open(&bus,"inverter.home.arpa",port);
        assert(connected == (i != 1));
        if (connected) {
            struct sockaddr_in addr;
            socklen_t len = sizeof(addr);
            assert(!getpeername(bus.fd,(struct sockaddr *)&addr,&len));
            assert(ntohl(addr.sin_addr.s_addr) == (i == 0 ? 0x7f000001u : 0x7f000002u));
        }
        inv_mb_close(&bus);
    }
    assert(lookups == 3);
    puts("PASS: unchanged DHCP hostname, transient DNS failure and reconnection to new IP on the configured port");
}
