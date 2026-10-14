#!/usr/bin/env python3
"""Keep SDIO DMA buffers internal, but queue CPU-only streaming copies in PSRAM."""
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
    print('ESP-Hosted streaming RX copies use PSRAM; packet allocation failures are recoverable')
