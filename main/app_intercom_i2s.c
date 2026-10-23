/* A BSP 2.0.3 hat DMA-puffert kér irányonként, 240 mintával. A kaputelefon
 * 16 kHz-es duplex PCM-jéhez három, 10 ms-os puffer elegendő: 11 520 helyett
 * 3 840 bájt mintapuffer. A gyártói komponens és a többi I2S-hívás változatlan. */
#include "driver/i2s_std.h"
#include "sdkconfig.h"

esp_err_t __real_i2s_new_channel(const i2s_chan_config_t *config,
                               i2s_chan_handle_t *tx, i2s_chan_handle_t *rx);

esp_err_t __wrap_i2s_new_channel(const i2s_chan_config_t *config,
                               i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    if (!config || !tx || !rx || config->id != CONFIG_BSP_I2S_NUM) {
        return __real_i2s_new_channel(config, tx, rx);
    }
    i2s_chan_config_t bounded = *config;
    if (bounded.dma_desc_num > 3) {
        bounded.dma_desc_num = 3;
    }
    if (bounded.dma_frame_num > 160) {
        bounded.dma_frame_num = 160;
    }
    return __real_i2s_new_channel(&bounded, tx, rx);
}
