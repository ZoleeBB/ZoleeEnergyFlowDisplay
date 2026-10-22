#pragma once
#include <stdbool.h>
typedef void *esp_codec_dev_handle_t;
typedef struct { int sample_rate, channel, bits_per_sample; } esp_codec_dev_sample_info_t;
#define ESP_CODEC_DEV_OK 0
int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *format);
int esp_codec_dev_close(esp_codec_dev_handle_t handle);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t handle, bool mute);
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t handle, float gain);
int esp_codec_dev_read(esp_codec_dev_handle_t handle, void *data, int size);
int esp_codec_dev_write(esp_codec_dev_handle_t handle, void *data, int size);
