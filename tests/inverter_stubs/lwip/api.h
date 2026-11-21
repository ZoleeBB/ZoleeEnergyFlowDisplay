#pragma once
#include <stdint.h>
#define LWIP_IPV6 1
#define NETCONN_DNS_IPV4 0
#define NETCONN_DNS_IPV6 1
#define NETCONN_DNS_IPV4_IPV6 2
#define NETCONN_DNS_IPV6_IPV4 3
#define ERR_OK 0
#define ERR_VAL -6
typedef uint8_t u8_t;
typedef int err_t;
typedef struct { uint32_t v4; struct { uint32_t addr[4]; uint8_t zone; } v6; } ip_addr_t;
#define ip_addr_set_ip4_u32(p, value) ((p)->v4 = (value))
#define ip_addr_copy_from_ip6(p, value) do { memcpy((p).v6.addr, (value).addr, 16); (p).v6.zone = (value).zone; } while(0)
#define ip_addr_isany(p) (!(p)->v6.addr[0] && !(p)->v6.addr[1] && !(p)->v6.addr[2] && !(p)->v6.addr[3])
int lwip_hook_netconn_external_resolve(const char *, ip_addr_t *, u8_t, err_t *);
