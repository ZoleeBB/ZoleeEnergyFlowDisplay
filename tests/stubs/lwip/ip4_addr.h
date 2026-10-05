#pragma once
#include <arpa/inet.h>
typedef struct in_addr ip4_addr_t;
static inline int ip4addr_aton(const char *text,ip4_addr_t *address){return inet_pton(AF_INET,text,address)==1;}
