#!/usr/bin/env python3
"""Bound SDIO DMA to one static window; store streaming data and queue copies in PSRAM."""
import argparse
from pathlib import Path

REPLACEMENTS = [
    ('#include "esp_hosted_transport_init.h"',
     '#include "esp_hosted_transport_init.h"\n#include "esp_heap_caps.h"'),
    ('buf_handle.free_buf_handle    = sdio_buffer_free;',
     '''#if H_SDIO_HOST_RX_MODE == H_SDIO_HOST_STREAMING_MODE
	/* Streaming packet copies are CPU-only; SDIO DMA buffers stay internal. */
	buf_handle.free_buf_handle    = heap_caps_free;
#else
	buf_handle.free_buf_handle    = sdio_buffer_free;
#endif'''),
    ('''		/* Allocate rx buffer */
		pkt_rxbuff = sdio_buffer_alloc(MEMSET_REQUIRED);
		assert(pkt_rxbuff);

		packet_size = len + offset;
		if (packet_size > buf_len) {
			ESP_LOGE(TAG, "packet size too big for remaining stream data");
			return ESP_FAIL;
		}
		memcpy(pkt_rxbuff, buf, packet_size);

		if (sdio_push_pkt_to_queue(pkt_rxbuff, len, offset)) {
			ESP_LOGI(TAG, "Failed to push a packet to queue from stream");
		}''',
     '''		packet_size = len + offset;
		if (packet_size > buf_len) {
			ESP_LOGE(TAG, "packet size too big for remaining stream data");
			return ESP_FAIL;
		}
		/* This copy never enters SDIO DMA. Avoid consuming internal DMA RAM. */
		pkt_rxbuff = heap_caps_calloc(1, packet_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!pkt_rxbuff) {
			ESP_LOGW(TAG, "RX packet copy unavailable; dropping remaining stream");
			return ESP_ERR_NO_MEM;
		}
		memcpy(pkt_rxbuff, buf, packet_size);

		if (sdio_push_pkt_to_queue(pkt_rxbuff, len, offset)) {
			heap_caps_free(pkt_rxbuff);
			ESP_LOGI(TAG, "Failed to push a packet to queue from stream");
		}'''),
    ('''				assert(copy_payload);
				assert(buf_handle->payload_len);''',
     '''				if (!copy_payload) {
					H_FREE_PTR_WITH_FUNC(buf_handle->free_buf_handle, buf_handle->priv_buffer_handle);
					ESP_LOGW(TAG, "Network RX payload unavailable; dropping packet");
					continue;
				}
				assert(buf_handle->payload_len);'''),
]

# Keep large streaming storage outside DMA RAM. Only a fixed aligned 1 KiB
# window ever reaches SDMMC; partial FIFO reads encode their own actual length.
REPLACEMENTS += [
    ('#include "esp_heap_caps.h"', '#include "esp_heap_caps.h"\n#include "esp_attr.h"\n#if H_SDIO_HOST_RX_MODE == H_SDIO_HOST_STREAMING_MODE\nDMA_ATTR static uint8_t sdio_rx_dma_scratch[1024] __attribute__((aligned(64)));\n#endif'),
    ('''
		if (*buf) {
			// free already allocated memory
			g_h.funcs->_h_free(*buf);
		}
		*buf = (uint8_t *)MEM_ALLOC(len);
		assert(*buf);''', '''
		uint8_t *next = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!next) {
			ESP_LOGW(TAG, "RX stream storage unavailable: %u bytes; draining stream", (unsigned)len);
			return NULL;
		}
		heap_caps_free(*buf);
		*buf = next;'''),
    ('\t\trxbuff = sdio_rx_get_buffer(len_from_slave);\n\t\tassert(rxbuff);', '\t\trxbuff = sdio_rx_get_buffer(len_from_slave);\n#if H_SDIO_HOST_RX_MODE != H_SDIO_HOST_STREAMING_MODE\n\t\tassert(rxbuff);\n#endif'),
    ('\t\t\tlen_to_read = data_left;', '\t\t\tlen_to_read = data_left;\n#if H_SDIO_HOST_RX_MODE == H_SDIO_HOST_STREAMING_MODE\n\t\t\tif (len_to_read > sizeof(sdio_rx_dma_scratch)) len_to_read = sizeof(sdio_rx_dma_scratch);\n#endif'),
    ('ESP_SLAVE_CMD53_END_ADDR - data_left,\n\t\t\t\t\tpos, block_read_len', 'ESP_SLAVE_CMD53_END_ADDR - len_to_read,\n#if H_SDIO_HOST_RX_MODE == H_SDIO_HOST_STREAMING_MODE\n\t\t\t\t\tsdio_rx_dma_scratch, block_read_len\n#else\n\t\t\t\t\tpos, block_read_len\n#endif\n'),
    ('ESP_SLAVE_CMD53_END_ADDR - data_left,\n\t\t\t\t\tpos, len_to_read', 'ESP_SLAVE_CMD53_END_ADDR - len_to_read,\n#if H_SDIO_HOST_RX_MODE == H_SDIO_HOST_STREAMING_MODE\n\t\t\t\t\tsdio_rx_dma_scratch, len_to_read\n#else\n\t\t\t\t\tpos, len_to_read\n#endif\n'),
    ('\t\t\tdata_left -= len_to_read;\n\t\t\tpos += len_to_read;', '''#if H_SDIO_HOST_RX_MODE == H_SDIO_HOST_STREAMING_MODE
			if (rxbuff) memcpy(pos, sdio_rx_dma_scratch, len_to_read);
#endif
			data_left -= len_to_read;
			if (pos) pos += len_to_read;'''),
    ('\t\tif (unlikely(ret))\n\t\t\tcontinue;\n\n\t\tif (double_buf.read_index < 0) {', '\t\tif (unlikely(ret))\n\t\t\tcontinue;\n\n\t\tif (!rxbuff) continue; /* Drained on storage OOM; FIFO byte count is updated. */\n\t\tif (double_buf.read_index < 0) {'),
    ('		sdio_rx_byte_count += len_from_slave;', '		sdio_rx_byte_count += len_from_slave - data_left; /* Only completed DMA reads consumed FIFO bytes. */'),
]

def patch(source):
    for old, new in REPLACEMENTS:
        if source.count(new) == 1 and source.replace(new, '').count(old) == 0:
            continue
        if source.count(old) != 1 or new in source:
            raise ValueError('Unsupported ESP-Hosted SDIO source; expected 1.4.7 RX path')
        source = source.replace(old, new)
    return source

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = patch(args.input.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print('ESP-Hosted RX uses a bounded 1 KiB DMA window and PSRAM; storage OOM drains the FIFO')
