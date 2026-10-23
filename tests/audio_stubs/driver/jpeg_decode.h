#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdlib.h>
typedef void *jpeg_decoder_handle_t;
typedef struct { unsigned width, height, sample_method; } jpeg_decode_picture_info_t;
typedef struct { int timeout_ms; } jpeg_decode_engine_cfg_t;
typedef struct { int buffer_direction; } jpeg_decode_memory_alloc_cfg_t;
typedef struct { int output_format, rgb_order; } jpeg_decode_cfg_t;
#define JPEG_DOWN_SAMPLING_GRAY 0
#define JPEG_DOWN_SAMPLING_YUV444 1
#define JPEG_DEC_ALLOC_OUTPUT_BUFFER 1
#define JPEG_DEC_ALLOC_INPUT_BUFFER 2
#define JPEG_DECODE_OUT_FORMAT_RGB565 1
#define JPEG_DECODE_OUT_FORMAT_GRAY 2
#define JPEG_DEC_RGB_ELEMENT_ORDER_BGR 1
esp_err_t jpeg_decoder_get_info(const uint8_t *, uint32_t, jpeg_decode_picture_info_t *);
void *jpeg_alloc_decoder_mem(size_t, const jpeg_decode_memory_alloc_cfg_t *, size_t *);
esp_err_t jpeg_new_decoder_engine(const jpeg_decode_engine_cfg_t *, jpeg_decoder_handle_t *);
esp_err_t jpeg_decoder_process(jpeg_decoder_handle_t, const jpeg_decode_cfg_t *, const uint8_t *, uint32_t, uint8_t *, uint32_t, uint32_t *);
esp_err_t jpeg_del_decoder_engine(jpeg_decoder_handle_t);
