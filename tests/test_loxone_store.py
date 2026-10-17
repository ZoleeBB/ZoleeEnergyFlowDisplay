#!/usr/bin/env python3
"""Requires the real mbedTLS development library (CI installs libmbedtls-dev)."""
import os
import subprocess
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-secret-') as tmp:
    cmd=[os.environ.get('CC','cc'),'-std=gnu11','-Wall','-Wextra','-Werror',
         '-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-Itests/loxone_stubs','-Itests/stubs','-Imain',
         f'-DAPP_SD_MOUNT_POINT="{tmp}"','tests/test_loxone_store.c',
         'main/app_loxone_store.c','main/app_loxone_model.c',
         '-lmbedcrypto','-l:libcjson.so.1','-lm','-Wl,--wrap=rename','-Wl,--wrap=fsync','-o',tmp+'/test']
    include=os.environ.get('LOXONE_MBEDTLS_INCLUDE')
    lib=os.environ.get('LOXONE_MBEDTLS_LIB')
    if include:cmd[1:1]=['-I'+include]
    if lib:cmd[1:1]=['-L'+lib]
    subprocess.run(cmd,cwd=root,check=True)
    subprocess.run([tmp+'/test'],check=True,timeout=15)
