#!/usr/bin/env python3
"""Audio valódi C-modell, RSA/AES és korlátos képfeldolgozás."""
import os
import struct
import subprocess
import tempfile
from pathlib import Path
import zlib

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
CC = os.environ.get('CC', 'cc')
COMMON = [CC, '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Werror=frame-larger-than=8192',
          '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain']


def run(command, **kwargs):
    return subprocess.run(command, cwd=ROOT, check=True, **kwargs)


def png_chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))


def png_rgba(width, height, raw, filter_type=0):
    return (b'\x89PNG\r\n\x1a\n' +
            png_chunk(b'IHDR', struct.pack('!IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            png_chunk(b'IDAT', zlib.compress(bytes([filter_type]) + raw)) + png_chunk(b'IEND', b''))


with tempfile.TemporaryDirectory(prefix='loxone-audio-') as directory:
    temp = Path(directory)
    model = str(temp / 'model')
    run(COMMON + ['tests/test_audio.c', 'main/app_loxone_model.c', '-l:libcjson.so.1', '-lm', '-o', model])
    run([model], timeout=20)
    runtime = str(temp / 'runtime')
    runtime_flags = COMMON[:COMMON.index('-Itests/stubs')]
    run(runtime_flags + ['-Itests/loxone_audio_stubs', '-Itests/loxone_stubs', '-Itests/audio_stubs',
                         '-Itests/stubs', '-Imain', 'tests/test_audio_runtime.c',
                         'main/app_loxone_model.c', 'main/app_loxone_home_model.c',
                         '-l:libcjson.so.1', '-lm', '-o', runtime])
    run([runtime], timeout=20)
    media = str(temp / 'media')
    run(runtime_flags + ['-Itests/loxone_audio_stubs', '-Itests/stubs', '-Imain',
                         'tests/test_audio_media.c', 'main/app_loxone_model.c',
                         '-l:libcjson.so.1', '-lm', '-o', media])
    run([media], timeout=20)

    private, public = temp / 'private.pem', temp / 'public.pem'
    run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048', '-out', str(private)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    crypto = COMMON.copy()
    include = os.environ.get('LOXONE_MBEDTLS_INCLUDE')
    library = os.environ.get('LOXONE_MBEDTLS_LIB')
    if include:
        crypto += ['-I' + include]
    if library:
        crypto += ['-L' + library]
    auth = str(temp / 'auth')
    run(crypto + ['tests/test_audio_auth.c', 'main/app_loxone_audio_auth.c', '-lmbedcrypto', '-o', auth])
    run([auth, str(private), str(public)], timeout=20)

    decoder = str(temp / 'image')
    run(COMMON + ['-Itests/audio_image_stubs', 'tests/test_audio_image.c', 'main/app_loxone_audio_image.c',
                  '-lz', '-o', decoder])

    def decoded(path, size=16, binary=decoder):
        result = run([binary, str(path), str(size)], capture_output=True, text=True, timeout=15).stdout
        if result.strip() == 'REJECTED':
            return None
        dimensions, pixels = result.strip().splitlines()
        return tuple(map(int, dimensions.split())), [int(value, 16) for value in pixels.split()]

    def reference(image, size):
        width, height = image.size
        target = (size, max(1, height * size // width)) if width >= height else (max(1, width * size // height), size)
        rgba = image.convert('RGBA')
        result = []
        for y in range(target[1]):
            for x in range(target[0]):
                red, green, blue, alpha = rgba.getpixel((x * width // target[0], y * height // target[1]))
                red = (red * alpha + 32 * (255 - alpha) + 127) // 255
                green = (green * alpha + 52 * (255 - alpha) + 127) // 255
                blue = (blue * alpha + 58 * (255 - alpha) + 127) // 255
                result.append((red >> 3) << 11 | (green >> 2) << 5 | blue >> 3)
        return target, result

    rgba = Image.new('RGBA', (47, 35))
    rgba.putdata([((x * 37 + y * 3) % 256, (x + y * 23) % 256, (x * 5 + y * 7) % 256,
                   (x * 11 + y * 13) % 256) for y in range(35) for x in range(47)])
    for mode in ('RGBA', 'RGB', 'LA', 'L'):
        path = temp / (mode + '.png')
        image = rgba.convert(mode)
        image.save(path)
        assert decoded(path, 28) == reference(image, 28), mode
    for depth in (1, 2, 4, 8):
        image = rgba.convert('RGB').quantize(colors=2 ** depth)
        image.info['transparency'] = bytes([i * 255 // max(1, 2 ** depth - 1) for i in range(2 ** depth)])
        path = temp / f'palette-{depth}.png'
        image.save(path, bits=depth)
        with Image.open(path) as loaded:
            assert decoded(path, 23) == reference(loaded, 23), depth
    path = temp / 'gray-1.png'
    rgba.convert('1').save(path)
    with Image.open(path) as loaded:
        assert decoded(path, 16) == reference(loaded, 16)

    path = temp / 'bad.png'
    valid = (temp / 'RGBA.png').read_bytes()
    corrupt = bytearray(valid)
    corrupt[-5] ^= 1
    path.write_bytes(corrupt)
    assert decoded(path) is None, 'CRC corruption accepted'
    path.write_bytes(valid[:-5])
    assert decoded(path) is None, 'truncated PNG accepted'
    path.write_bytes(png_rgba(513, 1, bytes(513 * 4)))
    assert decoded(path) is None, 'oversize source accepted'
    path.write_bytes(png_rgba(1, 1, bytes(4), filter_type=5))
    assert decoded(path) is None, 'invalid filter accepted'
    path.write_bytes(valid + b'x')
    assert decoded(path) is None, 'trailing content accepted'
    path.write_bytes(valid)
    assert decoded(path, 257) is None, 'oversize output accepted'
    path.write_bytes(valid + bytes(256 * 1024))
    assert decoded(path) is None, 'oversize compressed accepted'
    print('PASS: PNG RGB/RGBA/gray/palette, alpha, limits, CRC and malformed-input rejection')

    lvgl = os.environ.get('LVGL_SOURCE_DIR')
    if lvgl:
        jpeg_decoder = str(temp / 'jpeg')
        run(COMMON + ['-DLV_CONF_SKIP', '-DLV_USE_TJPGD=1', '-I' + lvgl,
                      'tests/test_audio_image.c', 'main/app_loxone_audio_image.c',
                      str(Path(lvgl) / 'src/libs/tjpgd/tjpgd.c'), '-lz', '-o', jpeg_decoder])
        path = temp / 'cover.jpg'
        Image.new('RGB', (128, 64), (240, 80, 32)).save(path, quality=95)
        dimensions, pixels = decoded(path, 32, jpeg_decoder)
        assert dimensions == (32, 16) and len(pixels) == 512
        assert all(abs(((value >> 11) << 3) - 240) <= 8 for value in pixels)
        Image.new('RGB', (513, 64), (240, 80, 32)).save(path)
        assert decoded(path, 32, jpeg_decoder) is None
        print('PASS: actual TJpgDec JPEG decoding and source size rejection')
    else:
        print('JPEG host test skipped: set LVGL_SOURCE_DIR; firmware/preview still build the actual decoder.')
