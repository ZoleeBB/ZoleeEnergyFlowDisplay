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


def jpeg_private_build_guard(temp, lvgl):
    """A valódi ESP képkód optimalizált keretei és a szűk EOF-kivétel határa."""
    source = (ROOT / 'main/app_loxone_audio_image.c').read_text(encoding='utf-8')
    memory = (ROOT / 'tests/test_audio_image_memory.c').read_text(encoding='utf-8')
    memory = memory.replace('"../main/app_loxone_audio_image.c"',
                            '"' + str(ROOT / 'main/app_loxone_audio_image.c') + '"')
    flags = [CC, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-Werror=frame-larger-than=1536', '-fstack-usage', '-DESP_PLATFORM',
             '-DLV_KCONFIG_IGNORE', '-DLV_CONF_SKIP', '-DLV_USE_TJPGD=1',
             '-Itests/loxone_audio_stubs', '-Itests/stubs', '-Imain', '-I' + lvgl]
    probes = [
        ('before', source, '#define STB_IMAGE_STATIC', False),
        ('after', source, 'static size_t audio_jpeg_read(', False),
        ('harness', memory, 'static void jpeg_alignment_test(', True),
    ]
    for optimization in ('O2', 'Os'):
        obj = temp / ('image-private-' + optimization + '.o')
        run(flags + ['-' + optimization, '-c', 'main/app_loxone_audio_image.c', '-o', str(obj)])
        records = [line.split('\t') for line in obj.with_suffix('.su').read_text().splitlines()]
        assert records and any('lx_audio_image_decode' in record[0] for record in records)
        assert all(int(record[1]) <= 1536 and record[2] in ('static', 'dynamic,bounded')
                   for record in records), (optimization, records)
        assert not any('stbi__vertical_flip' in record[0] for record in records)
        symbols = run([os.environ.get('NM', 'nm'), '--defined-only', str(obj)],
                      capture_output=True, text=True).stdout
        assert 'stbi__vertical_flip' not in symbols, optimization
        exported = run([os.environ.get('NM', 'nm'), '-g', '--defined-only', str(obj)],
                       capture_output=True, text=True).stdout
        assert not any('stbi_' in line.split()[-1] for line in exported.splitlines()), exported
        for name, original, marker, harness in probes:
            assert original.count(marker) == 1, marker
            function = 'audio_own_unused_' + name
            modified = original.replace(marker, 'static void ' + function + '(void) {}\n' + marker)
            path = temp / ('image-warning-' + name + '.c')
            path.write_text(modified, encoding='utf-8')
            probe_flags = [flag for flag in flags if not (harness and flag == '-DESP_PLATFORM')]
            result = subprocess.run(probe_flags + ['-' + optimization, '-c', str(path),
                                    '-o', str(path.with_suffix('.o'))],
                                    cwd=ROOT, capture_output=True, text=True)
            assert result.returncode != 0 and function in result.stderr and \
                '-Werror=unused-function' in result.stderr, (name, optimization, result.stderr)
        print('PASS: private JPEG', optimization, 'max frame', max(int(r[1]) for r in records),
              'bytes; no flip/exported STB API; own warnings before/after vendor and C include retained')


def png_chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))


def png_rgba(width, height, raw, filter_type=0):
    return (b'\x89PNG\r\n\x1a\n' +
            png_chunk(b'IHDR', struct.pack('!IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            png_chunk(b'IDAT', zlib.compress(bytes([filter_type]) + raw)) + png_chunk(b'IEND', b''))


def asymmetric_jpeg(width, height):
    """Saját egyszínű JPEG: a valódi rádiólogó Y=2x2, Cb=1x1, Cr=2x2 mintája."""
    def segment(marker, payload):
        return bytes([255, marker]) + struct.pack('!H', len(payload) + 2) + payload

    sampling = ((2, 2), (1, 1), (2, 2))
    result = b'\xff\xd8'
    result += segment(0xdb, b'\0' + bytes([16] * 64) + b'\1' + bytes([16] * 64))
    frame = bytes([8]) + struct.pack('!HHB', height, width, 3)
    for index, (horizontal, vertical) in enumerate(sampling):
        frame += bytes([index + 1, horizontal * 16 + vertical, 0 if index == 0 else 1])
    result += segment(0xc0, frame)
    tables = b''
    for table in (0, 1):
        tables += bytes([table]) + bytes([0, 0, 0, 12] + [0] * 12) + bytes(range(12))
        tables += bytes([0x10 + table]) + bytes([1] + [0] * 15) + b'\0'
    result += segment(0xc4, tables)
    result += segment(0xda, bytes([3, 1, 0, 2, 0x11, 3, 0x11, 0, 63, 0]))
    previous = [0, 0, 0]
    dc = [-16, -8, 16]
    bits = ''
    for _ in range(((width + 15) // 16) * ((height + 15) // 16)):
        for component, (horizontal, vertical) in enumerate(sampling):
            for _ in range(horizontal * vertical):
                difference = dc[component] - previous[component]
                previous[component] = dc[component]
                category = abs(difference).bit_length()
                bits += f'{category:04b}'
                if category:
                    value = difference if difference > 0 else (1 << category) - 1 + difference
                    bits += f'{value:0{category}b}'
                bits += '0'  # A többi AC-együttható nulla.
    bits += '1' * (-len(bits) % 8)
    entropy = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
    return result + entropy.replace(b'\xff', b'\xff\0') + b'\xff\xd9'


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

    crypto = COMMON.copy()
    include = os.environ.get('LOXONE_MBEDTLS_INCLUDE')
    library = os.environ.get('LOXONE_MBEDTLS_LIB')
    if include:
        crypto += ['-I' + include]
    if library:
        crypto += ['-L' + library]
    auth = str(temp / 'auth')
    run(crypto + ['tests/test_audio_auth.c', 'main/app_loxone_audio_auth.c', '-lmbedcrypto', '-o', auth])
    for bits in (2048, 4096):
        private, public = temp / ('private-' + str(bits) + '.pem'), temp / ('public-' + str(bits) + '.pem')
        run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:' + str(bits), '-out', str(private)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        jpeg_private_build_guard(temp, lvgl)
        jpeg_decoder = str(temp / 'jpeg')
        run(COMMON + ['-DLV_CONF_SKIP', '-DLV_USE_TJPGD=1', '-I' + lvgl,
                      'tests/test_audio_image.c', 'main/app_loxone_audio_image.c',
                      str(Path(lvgl) / 'src/libs/tjpgd/tjpgd.c'), '-lz', '-o', jpeg_decoder])
        path = temp / 'cover.jpg'
        Image.new('RGB', (128, 64), (240, 80, 32)).save(path, quality=95)
        dimensions, pixels = decoded(path, 32, jpeg_decoder)
        assert dimensions == (32, 16) and len(pixels) == 512
        assert all(abs(((value >> 11) << 3) - 240) <= 8 for value in pixels)
        path.write_bytes(path.read_bytes() + b'\0\r\n')
        assert decoded(path, 32, jpeg_decoder) == (dimensions, pixels), 'Baseline JPEG trailer regression'
        spotify = temp / 'spotify-size.jpg'
        Image.new('RGB', (640, 640), (240, 80, 32)).save(spotify, quality=95, subsampling=0)
        for size in (112, 256):
            dimensions, pixels = decoded(spotify, size, jpeg_decoder)
            assert dimensions == (size, size) and len(pixels) == size * size
        radio = temp / 'radio-sampling.jpg'
        radio.write_bytes(asymmetric_jpeg(145, 145))
        with Image.open(radio) as reference_radio:
            expected = reference(reference_radio, 112)
            dimensions, pixels = decoded(radio, 112, jpeg_decoder)
            assert dimensions == (112, 112)
            assert pixels == expected[1], 'Asymmetric JPEG component sampling changed the colors'
        progressive = temp / 'progressive-cmyk.jpg'
        Image.new('CMYK', (512, 512), (20, 40, 90, 10)).save(progressive, progressive=True)
        assert decoded(progressive, 256, jpeg_decoder)[0] == (256, 256)
        Image.new('RGB', (513, 64), (240, 80, 32)).save(path, progressive=True)
        assert decoded(path, 32, jpeg_decoder) is None, 'Fallback source limit exceeded'
        Image.new('RGB', (1025, 64), (240, 80, 32)).save(path)
        assert decoded(path, 32, jpeg_decoder) is None
        path.write_bytes(radio.read_bytes()[:-2])
        assert decoded(path, 32, jpeg_decoder) is None, 'Truncated fallback JPEG accepted'
        memory = str(temp / 'image-memory')
        run(COMMON + ['-Itests/loxone_audio_stubs', '-DLV_CONF_SKIP', '-DLV_USE_TJPGD=1',
                      '-I' + lvgl, 'tests/test_audio_image_memory.c',
                      str(Path(lvgl) / 'src/libs/tjpgd/tjpgd.c'), '-lz', '-o', memory])
        run([memory, str(radio), str(spotify), str(progressive)], timeout=20)
        print('PASS: actual TJpgDec 640px JPEG, asymmetric chroma, bounded progressive fallback and truncation rejection')
    else:
        print('JPEG host test skipped: set LVGL_SOURCE_DIR; firmware/preview still build the actual decoder.')
