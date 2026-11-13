#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_RTCRAM 8
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
