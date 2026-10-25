#!/usr/bin/env python3
"""Valódi RIFF-parser: törlés, fájlonkénti telepítés és részletes hibajelentés."""
import ctypes as C
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave
import zipfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='intercom-ringtones-') as directory:
    folder = Path(directory)
    (folder / 'esp_heap_caps.h').write_text('''#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_8BIT 2
void *heap_caps_malloc(size_t size, unsigned caps);
''')
    stub = folder / 'stub.c'
    stub.write_text('''
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "app_storage.h"
#include "app_ringtone_wav.h"
uint32_t test_frames(uint32_t bytes, uint32_t rate) { return app_ringtone_frames(bytes, rate); }
uint32_t test_source(uint32_t frame, uint32_t rate) { return app_ringtone_source_frame(frame, rate); }
static bool busy, bounded_reads, fail_reads, fail_memory, fail_seek, locked;
static bool fail_create, fail_close;
static FILE *close_target;
void file_faults(bool create, bool close) { fail_create = create; fail_close = close; }
FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode) {
    if (fail_create && strstr(path, "write-fails.wav") && !strcmp(mode, "wb")) { errno = EIO; return NULL; }
    FILE *file = __real_fopen(path, mode);
    if (fail_close && strstr(path, "close-fails.wav") && !strcmp(mode, "wb")) close_target = file;
    return file;
}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *file) {
    bool fail = file == close_target;
    if (fail) close_target = NULL;
    int result = __real_fclose(file);
    if (fail) { errno = EIO; return EOF; }
    return result;
}
bool storage_locked(void) { return locked; }
void fail_position(bool value) { fail_seek = value; }
int __real_fseek(FILE *, long, int);
int __wrap_fseek(FILE *file, long position, int origin) {
    return fail_seek ? -1 : __real_fseek(file, position, origin);
}
void fail_allocation(bool value) { fail_memory = value; }
void *heap_caps_malloc(size_t size, unsigned caps) {
    assert(caps == (4 | 2));
    return fail_memory ? NULL : malloc(size);
}
void limit_reads(bool value) { bounded_reads = value; }
void fail_read(bool value) { fail_reads = value; }
size_t __real_fread(void *, size_t, size_t, FILE *);
size_t __wrap_fread(void *data, size_t size, size_t count, FILE *file) {
    if (fail_reads || (bounded_reads && size * count > 2048)) return 0;
    return __real_fread(data, size, count, file);
}
void storage_busy(bool value) { busy = value; }
esp_err_t app_storage_lock(void) { assert(!locked); locked = true; return ESP_OK; }
bool app_storage_try_lock(void) { if (busy) return false; assert(!locked); locked = true; return true; }
void app_storage_unlock(void) { assert(locked); locked = false; }
bool app_templates_safe_asset(const char *name) { (void)name; return false; }
''')
    libpath = folder / 'ringtones.so'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-shared', '-fPIC', '-g', '-O1',
        '-Wall', '-Wextra', '-Werror', '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-DESP_PLATFORM', '-I' + directory, '-Itests/stubs', '-Imain', '-DAPP_SD_MOUNT_POINT="' + directory + '"',
        'main/app_ringtone_wav.c', 'main/app_ringtone_zip.c', 'main/app_ringtones.c', str(stub),
        '-Wl,--wrap=fread', '-Wl,--wrap=fseek', '-Wl,--wrap=fopen', '-Wl,--wrap=fclose', '-lz', '-l:libcjson.so.1', '-o', str(libpath)], cwd=ROOT, check=True)
    # A külön kibontó és a visszaállított témakibontó Release buildben is forduljon.
    for source in ('main/app_template_zip.c', 'main/app_ringtone_zip.c'):
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O3', '-Wall', '-Wextra', '-Werror',
            '-Wformat-truncation=2', '-Werror=frame-larger-than=' + ('4096' if 'ringtone' in source else '8192'), '-Itests/stubs', '-Imain', '-c', source,
            '-o', str(folder / (Path(source).stem + '.o'))], cwd=ROOT, check=True)
    receiver = folder / 'receive'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O3', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Itests/stubs', '-Imain',
        'tests/test_ringtone_receive.c', '-o', str(receiver)], cwd=ROOT, check=True)
    subprocess.run([str(receiver), str(folder / 'received.zip')], check=True)
    lib = C.CDLL(str(libpath))
    lib.app_ringtone_wav.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_size_t)]
    lib.app_ringtone_wav.restype = C.c_bool
    lib.app_ringtones_install.argtypes = [C.c_char_p, C.c_char_p]
    lib.app_ringtones_install_prepared.argtypes = [C.c_char_p, C.POINTER(C.c_void_p), C.c_char_p]
    lib.app_ringtones_reset.argtypes = [C.c_char_p]
    lib.storage_locked.restype = C.c_bool
    lib.app_ringtones_load.argtypes = [C.c_char_p, C.POINTER(C.c_size_t)]
    lib.app_ringtones_load.restype = C.c_void_p
    lib.app_ringtones_load_ex.argtypes = [C.c_char_p, C.POINTER(C.c_size_t), C.POINTER(C.c_int)]
    lib.app_ringtones_load_ex.restype = C.c_void_p
    lib.app_ringtones_load_detailed.argtypes = [C.c_char_p, C.POINTER(C.c_size_t), C.POINTER(C.c_int), C.c_char_p]
    lib.app_ringtones_load_detailed.restype = C.c_void_p
    status = C.c_int()
    detail = C.create_string_buffer(160)
    def load_status(name):
        size.value = 999
        pointer = lib.app_ringtones_load_ex(name, C.byref(size), C.byref(status))
        return pointer, status.value

    lib.app_ringtones_list.restype = C.c_void_p
    lib.cJSON_PrintUnformatted.argtypes = [C.c_void_p]; lib.cJSON_PrintUnformatted.restype = C.c_void_p
    lib.cJSON_Delete.argtypes = [C.c_void_p]
    libc = C.CDLL(None); libc.free.argtypes = [C.c_void_p]
    lib.test_frames.argtypes = [C.c_uint32, C.c_uint32]
    lib.test_source.argtypes = [C.c_uint32, C.c_uint32]
    assert lib.test_frames(1920000, 24000) == 1440000
    assert lib.test_frames(1920000, 16000) == 960000
    assert lib.test_source(1439999, 24000) == 959999
    offset, size = C.c_size_t(), C.c_size_t()
    sounds = {p.name: p.read_bytes() for p in (ROOT / 'assets/ringtones').glob('*.wav')}
    manifest = json.loads((ROOT / 'assets/ringtones/ringtone.json').read_text())
    expected = [entry['file'] for entry in manifest['sounds']]
    assert 1 <= len(expected) <= 32 and len(expected) == len(set(expected))
    assert set(sounds) == set(expected), 'Manifest and checked-in WAV set must match'
    sys.path.insert(0, str(ROOT / 'tools'))
    from prepare_ringtones import package_ringtones, ringtone_files
    shipped = ringtone_files()
    assert set(shipped) == set(sounds) | {'ringtone.json'}
    package = folder / 'release-ringtones.zip'
    package_ringtones(package)
    first_package = package.read_bytes()
    package_ringtones(package)
    assert package.read_bytes() == first_package
    with zipfile.ZipFile(package) as z:
        assert {name: z.read(name) for name in z.namelist()} == shipped
    # A nem CC0 metaadatot is változtatás nélkül őrizzük meg. A technikai korlátok élnek.
    fixture = folder / 'packaging'; fixture.mkdir()
    fixture_sound = next(iter(sounds))
    (fixture / fixture_sound).write_bytes(sounds[fixture_sound])
    metadata = {'sounds': [{'file': fixture_sound, 'license': 'User supplied; original source license'}]}
    def package_metadata(value):
        (fixture / 'ringtone.json').write_text(json.dumps(value))
        return ringtone_files(fixture)
    assert json.loads(package_metadata(metadata)['ringtone.json']) == metadata
    for invalid in ({'sounds': []}, {'sounds': metadata['sounds'] * 33},
                    {'sounds': metadata['sounds'] * 2},
                    {'sounds': [{'file': '../escape.wav', 'license': 'test'}]},
                    {'sounds': [{'file': fixture_sound}]}):
        try:
            package_metadata(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid package metadata accepted: ' + str(invalid))
    (fixture / fixture_sound).write_bytes(sounds[fixture_sound][:-1])
    try:
        package_metadata(metadata)
    except (ValueError, EOFError, wave.Error):
        pass
    else:
        raise AssertionError('Truncated source WAV accepted')
    for data in sounds.values():
        assert lib.app_ringtone_wav(data, len(data), C.byref(offset), C.byref(size))
        assert 0 < size.value <= 1920000
        for position, value in [(0, ord('X')), (20, 3), (22, 2), (24, 0), (34, 8)]:
            bad = bytearray(data); bad[position] = value
            assert not lib.app_ringtone_wav(bytes(bad), len(bad), C.byref(offset), C.byref(size)), position
        assert not lib.app_ringtone_wav(data[:-1], len(data) - 1, C.byref(offset), C.byref(size))
    def pcm(seconds, extra_frames=0):
        output = io.BytesIO()
        with wave.open(output, 'wb') as audio:
            audio.setnchannels(1); audio.setsampwidth(2); audio.setframerate(16000)
            audio.writeframes(b'\0\0' * (16000 * seconds + extra_frames))
        return output.getvalue()
    for seconds in (9, 37, 60):
        data = pcm(seconds)
        assert lib.app_ringtone_wav(data, len(data), C.byref(offset), C.byref(size))
    data = pcm(60, 1)
    assert not lib.app_ringtone_wav(data, len(data), C.byref(offset), C.byref(size))
    archive = folder / 'upload.zip'; error = C.create_string_buffer(160)
    def pack(files):
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            for name, data in files.items(): z.writestr(name, data)
    def install(files, success=True):
        pack(files)
        result = lib.app_ringtones_install(str(archive).encode(), error)
        assert (result == 0) == success, error.value.decode()
    # A tényleges kiadási csomagot a valódi C-telepítő is fogadja, mindegyik WAV egyezzen.
    assert lib.app_ringtones_install(str(package).encode(), error) == 0, error.value
    assert {p.name: p.read_bytes() for p in (folder / 'ringtones').iterdir()} == sounds
    install(sounds)
    # Azonos fájlnév új hanggal: ne legyen névütközés vagy régi tartalom.
    name = next(iter(sounds))
    replacement = pcm(1)
    install({name: replacement})
    assert (folder / 'ringtones' / name).read_bytes() == replacement
    install(sounds)
    def reported(files, success=True):
        pack(files)
        assert lib.app_ringtones_reset(error) == 0, error.value
        report = C.c_void_p()
        result = lib.app_ringtones_install_prepared(str(archive).encode(), C.byref(report), error)
        encoded = lib.cJSON_PrintUnformatted(report)
        parsed = json.loads(C.string_at(encoded))
        libc.free(encoded); lib.cJSON_Delete(report)
        assert (result == 0) == success, (error.value, parsed)
        assert not lib.storage_locked()
        return parsed

    # A régi és félbemaradt készlet a feltöltés előtt teljesen törlődik.
    for suffix in ('', '.old', '.new'):
        path = folder / ('ringtones' + suffix) / 'nested'
        path.mkdir(parents=True, exist_ok=True)
        (path / 'old.wav').write_bytes(b'old')
    assert lib.app_ringtones_reset(error) == 0
    assert list((folder / 'ringtones').iterdir()) == []
    assert not (folder / 'ringtones.old').exists() and not (folder / 'ringtones.new').exists()
    for bad in ({'../escape.wav': next(iter(sounds.values()))}, {'song.mp3': b'not-wav'},
                {'fake.wav': b'not-wav'}, {'big.wav': b'x' * 300000},
                {f'sound-{i}.wav': b'invalid' for i in range(34)}):
        report = reported(bad, False)
        assert report['installed'] == 0 and len(report['failed']) == len(bad)
        assert list((folder / 'ringtones').iterdir()) == []
        assert not (folder / 'escape.wav').exists()
    # Érvénytelen hang / SD-fájlmegnyitás / lezárás után is sorra kerülnek a jó fájlok.
    lib.file_faults(1, 1)
    report = reported({'before.wav': replacement, 'bad.wav': b'invalid',
                       'write-fails.wav': replacement, 'close-fails.wav': replacement,
                       'after.wav': replacement})
    lib.file_faults(0, 0)
    assert report['installed'] == 2, report
    assert {f['name'] for f in report['failed']} == {'bad.wav', 'write-fails.wav', 'close-fails.wav'}
    assert {p.name for p in (folder / 'ringtones').iterdir()} == {'before.wav', 'after.wav'}
    # A ZIP CRC-hibás eleme sem akadályozhatja a következő hangot.
    pack({'first.wav': replacement, 'crc.wav': replacement, 'last.wav': replacement})
    raw = bytearray(archive.read_bytes())
    with zipfile.ZipFile(archive) as zip:
        entry = zip.getinfo('crc.wav')
        struct.pack_into('<I', raw, entry.header_offset + 14, entry.CRC ^ 1)
    central = 0
    while True:
        central = raw.find(b'PK\x01\x02', central)
        if central < 0: break
        length = struct.unpack_from('<H', raw, central + 28)[0]
        if raw[central + 46:central + 46 + length] == b'crc.wav':
            struct.pack_into('<I', raw, central + 16, entry.CRC ^ 1)
        central += 46
    archive.write_bytes(raw)
    assert lib.app_ringtones_reset(error) == 0
    report = C.c_void_p()
    assert lib.app_ringtones_install_prepared(str(archive).encode(), C.byref(report), error) == 0
    encoded = lib.cJSON_PrintUnformatted(report); parsed = json.loads(C.string_at(encoded))
    libc.free(encoded); lib.cJSON_Delete(report)
    assert parsed['installed'] == 2 and parsed['failed'][0]['name'] == 'crc.wav', parsed
    # 33 hangból az első 32 marad, a többlet külön megnevezett hiba.
    report = reported({f'sound-{i}.wav': replacement for i in range(33)})
    assert report['installed'] == 32 and report['failed'][0]['name'] == 'sound-32.wav'
    install(sounds)
    name, data = next(iter(sounds.items()))
    pointer = lib.app_ringtones_load(name.encode(), C.byref(size))
    assert pointer and C.string_at(pointer, size.value) == data; libc.free(pointer)
    array = lib.app_ringtones_list(); text = lib.cJSON_PrintUnformatted(array)
    assert set(json.loads(C.string_at(text))) == set(sounds)
    libc.free(text); lib.cJSON_Delete(array)
    assert not lib.app_ringtones_load(b'../secret.wav', C.byref(size))
    lib.storage_busy(1)
    assert not lib.app_ringtones_load(name.encode(), C.byref(size))
    assert size.value == 0
    pointer, result = load_status(name.encode())
    assert pointer is None and result == 1 and size.value == 0
    lib.storage_busy(0)
    pointer, result = load_status(b'missing.wav')
    assert pointer is None and result == 2 and size.value == 0
    (folder / 'ringtones' / 'broken.wav').write_bytes(b'not a WAV')
    pointer, result = load_status(b'broken.wav')
    assert pointer is None and result == 5 and size.value == 0
    lib.fail_allocation(1)
    pointer, result = load_status(name.encode())
    assert pointer is None and result == 3 and size.value == 0
    lib.fail_allocation(0)
    lib.fail_read(1)
    pointer, result = load_status(name.encode())
    assert pointer is None and result == 4 and size.value == 0, (pointer, result, size.value)
    assert not lib.app_ringtones_load_detailed(name.encode(), C.byref(size), C.byref(status), detail)
    assert 'adatolvasás' in detail.value.decode() and name in detail.value.decode()
    lib.fail_read(0)
    lib.fail_position(1)
    # Az SD olvasó akkor is működjön, ha a seek nem támogatott.
    pointer = lib.app_ringtones_load_detailed(name.encode(), C.byref(size), C.byref(status), detail)
    assert pointer and status.value == 0 and not detail.value
    assert C.string_at(pointer, size.value) == data
    libc.free(pointer)
    lib.fail_position(0)
    # Pufferelt stdio: korlátozott olvasások egyedi DMA-kezelés nélkül.
    lib.limit_reads(1)
    pointer, result = load_status(name.encode())
    assert pointer and result == 0 and C.string_at(pointer, size.value) == data
    libc.free(pointer)
    print('PASS: strict WAV, delete before install, per-file CRC/PCM/SD error recovery, complete failure report, 32 sound limit, safe buffered list/load')
