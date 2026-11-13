#!/usr/bin/env python3
"""A meglévő natív redőnyikon változatlan SD-példányát készíti el."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def prepare():
    source = ROOT / 'assets/loxone-home/icons/blinds.png'
    target = ROOT / 'assets/loxone-home/icons/blinds-shading.png'
    data = source.read_bytes()
    if (data[:8] != b'\x89PNG\r\n\x1a\n' or data[12:16] != b'IHDR' or
            struct.unpack('>II', data[16:24]) != (96, 96) or data[25] != 6):
        raise ValueError('A redőnyikon forrásának 96×96-os RGBA PNG-nek kell lennie.')
    target.write_bytes(data)
    print(f'{target.relative_to(ROOT)}: {len(data)} bytes, 96×96 RGBA; változatlan forrás')


if __name__ == '__main__':
    prepare()
