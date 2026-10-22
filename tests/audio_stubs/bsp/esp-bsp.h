#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_audio_init(const void *configuration);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);
