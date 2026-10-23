#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_INTERCOM_JPEG_MAX (2U * 1024U * 1024U)
/* Inkrementális JPEG-keretezés. A MIME-fejlécet kihagyja, a JPEG metaadatok
 * hosszát tiszteletben tartja; TCP darabolás és túlméretes kép után is újraszinkronizál. */
typedef struct
{
    uint8_t *buffer;
    size_t capacity, used;
    unsigned state, remaining, length, marker;
    unsigned frame_starts, oversized_frames;
} app_intercom_stream_t;
typedef void (*app_intercom_frame_fn)(void *context, const uint8_t *jpeg, size_t size);
void app_intercom_stream_feed(app_intercom_stream_t *stream, const uint8_t *data,
                              size_t size, app_intercom_frame_fn frame, void *context);
