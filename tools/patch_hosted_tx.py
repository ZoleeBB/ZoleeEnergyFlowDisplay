#!/usr/bin/env python3
"""Propagate network TX buffer exhaustion instead of asserting in ESP-Hosted."""
import argparse
from pathlib import Path

OLD = '\tassert(copy_buff);'
NEW = '''	if (!copy_buff) {
		errno = ENOBUFS;
#if defined(ESP_ERR_ESP_NETIF_TX_FAILED)
		return ESP_ERR_ESP_NETIF_TX_FAILED;
#else
		return ESP_ERR_ESP_NETIF_NO_MEM;
#endif
	}'''

def patch(source):
    if source.count(NEW)==2 and source.replace(NEW,'').count(OLD)==0:return source
    if source.count(OLD)!=2:raise ValueError('Unsupported ESP-Hosted 1.4.7 STA/AP TX allocation')
    return source.replace(OLD,NEW)

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    fixed=patch(args.input.read_text())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(fixed)
    print('ESP-Hosted STA/AP TX allocation failures return a network error')
