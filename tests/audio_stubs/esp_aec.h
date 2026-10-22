#pragma once
#include <stdint.h>
typedef struct { int frame_size; } aec_handle_t;
#define AEC_MODE_FD_LOW_COST 5
aec_handle_t *aec_create(int rate, int filter, int channels, int mode);
int aec_get_chunksize(aec_handle_t *aec);
void aec_process(aec_handle_t *aec, int16_t *input, int16_t *reference, int16_t *output);
void aec_destroy(aec_handle_t *aec);
