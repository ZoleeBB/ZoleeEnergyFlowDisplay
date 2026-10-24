#!/usr/bin/env python3
"""Valódi RIFF-parser és SD ZIP-telepítő: visszaállítás és hibás archívumok."""
import ctypes as C
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave
import zipfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='intercom-ringtones-') as directory:
    folder = Path(directory)
    stub = folder / 'stub.c'
    stub.write_text('''
#include <stdbool.h>
#include <stdio.h>
#include "app_storage.h"
#include "app_ringtone_wav.h"
uint32_t test_frames(uint32_t bytes, uint32_t rate) { return app_ringtone_frames(bytes, rate); }
uint32_t test_source(uint32_t frame, uint32_t rate) { return app_ringtone_source_frame(frame, rate); }
static int fault, calls;
static bool busy;
void storage_busy(bool value) { busy = value; }
void fail_rename(int n) { fault = n; calls = 0; }
int __real_rename(const char *, const char *);
int __wrap_rename(const char *a, const char *b) { if (++calls == fault) return -1; return __real_rename(a, b); }
esp_err_t app_storage_lock(void) { return ESP_OK; }
bool app_storage_try_lock(void) { return !busy; }
void app_storage_unlock(void) {}
bool app_templates_safe_asset(const char *name) { (void)name; return false; }
''')
    libpath = folder / 'ringtones.so'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-shared', '-fPIC', '-g', '-O1',
        '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-Itests/stubs', '-Imain', '-DAPP_SD_MOUNT_POINT="' + directory + '"',
        'main/app_ringtone_wav.c', 'main/app_template_zip.c', 'main/app_ringtones.c', str(stub),
        '-Wl,--wrap=rename', '-lz', '-l:libcjson.so.1', '-o', str(libpath)], cwd=ROOT, check=True)
    lib = C.CDLL(str(libpath))
    lib.app_ringtone_wav.argtypes = [C.c_char_p, C.c_size_t, C.POINTER(C.c_size_t), C.POINTER(C.c_size_t)]
    lib.app_ringtone_wav.restype = C.c_bool
    lib.app_ringtones_install.argtypes = [C.c_char_p, C.c_char_p]
    lib.app_ringtones_load.argtypes = [C.c_char_p, C.POINTER(C.c_size_t)]
    lib.app_ringtones_load.restype = C.c_void_p
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
    assert set(sounds) == {'tubular-chime.wav'}
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
    install(sounds)
    original = {p.name: p.read_bytes() for p in (folder / 'ringtones').iterdir()}
    for bad in ({'../escape.wav': next(iter(sounds.values()))}, {'song.mp3': b'not-wav'},
                {'fake.wav': b'not-wav'}, {'big.wav': b'x' * 300000},
                {f'sound-{i}.wav': b'invalid' for i in range(34)}):
        install(bad, False)
        assert {p.name: p.read_bytes() for p in (folder / 'ringtones').iterdir()} == original
    for fail in (1, 2):
        lib.fail_rename(fail)
        install({'new.wav': next(iter(sounds.values()))}, False)
        lib.fail_rename(0)
        assert {p.name: p.read_bytes() for p in (folder / 'ringtones').iterdir()} == original
    # Simulated interruption between directory renames: a read restores the old complete set.
    (folder / 'ringtones').rename(folder / 'ringtones.old')
    name, data = next(iter(sounds.items()))
    pointer = lib.app_ringtones_load(name.encode(), C.byref(size))
    assert pointer and C.string_at(pointer, size.value) == data; libc.free(pointer)
    array = lib.app_ringtones_list(); text = lib.cJSON_PrintUnformatted(array)
    assert set(json.loads(C.string_at(text))) == set(sounds)
    libc.free(text); lib.cJSON_Delete(array)
    assert not lib.app_ringtones_load(b'../secret.wav', C.byref(size))
    lib.storage_busy(1)
    assert not lib.app_ringtones_load(name.encode(), C.byref(size))
    lib.storage_busy(0)
    print('PASS: tubular-chime CC0 WAV, strict PCM/rate/channel/depth/RIFF limits, ZIP traversal/type/count/size rejection, atomic rollback and power-loss recovery, safe list/load')
