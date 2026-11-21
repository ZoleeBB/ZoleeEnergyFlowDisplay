#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#define LWIP_PLATFORM_ASSERT(x) assert(0 && x)
#define LWIP_PLATFORM_DIAG(x) ((void)0)
