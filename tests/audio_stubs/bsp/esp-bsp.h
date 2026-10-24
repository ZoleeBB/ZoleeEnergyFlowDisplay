#pragma once
#include "esp_err.h"
#include "esp_codec_dev.h"
typedef struct {
    struct { int sample_rate; } clk_cfg;
    struct { int bits, mode; } slot_cfg;
    struct { int mclk, bclk, ws, dout, din; } gpio_cfg;
} i2s_std_config_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) { .sample_rate = (rate) }
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, slots) { .bits = (width), .mode = (slots) }
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
#define BSP_I2S_MCLK 13
#define BSP_I2S_SCLK 12
#define BSP_I2S_LCLK 10
#define BSP_I2S_DOUT 9
#define BSP_I2S_DSIN 11
esp_err_t bsp_i2c_init(void);
esp_err_t bsp_audio_init(const void *configuration);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

#define BSP_AUDIO_TDM_SLOT_COUNT 4
#define BSP_AUDIO_TDM_SLOT_MASK_FL 1
esp_err_t bsp_audio_init_voice_24k(void);
