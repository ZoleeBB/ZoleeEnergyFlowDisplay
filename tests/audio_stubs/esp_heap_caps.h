#pragma once
#include <stdlib.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, unsigned caps);

#define MALLOC_CAP_DMA 8
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);

void *heap_caps_malloc(size_t, unsigned);
