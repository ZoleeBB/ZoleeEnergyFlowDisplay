#pragma once
#include <stdint.h>
typedef struct { int mic_num, ref_num, out_num, filter_length, sample_rate; uint32_t caps; int mode, nlp_level; } aec_config_t;
typedef struct { int frame_size; } aec_handle_t;
#define AEC_MODE_FD_LOW_COST 5
#define AEC_NLP_LEVEL_AGGR 1
aec_handle_t *aec_create_from_config(aec_config_t *config);
aec_handle_t *aec_create(int rate, int filter, int channels, int mode);
int aec_get_chunksize(aec_handle_t *aec);
void aec_process(aec_handle_t *aec, int16_t *input, int16_t *reference, int16_t *output);
void aec_destroy(aec_handle_t *aec);
