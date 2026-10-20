#!/usr/bin/env python3
import os
import subprocess
import tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='loxone-protocol-') as tmp:
    subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',tmp+'/private.pem'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    subprocess.run(['openssl','pkey','-in',tmp+'/private.pem','-pubout','-out',tmp+'/public.pem'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    cmd=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-Wall','-Wextra','-Werror',
         '-Wno-misleading-indentation','-Wno-unused-function','-ffunction-sections','-fdata-sections',
         '-fsanitize=undefined','-fno-sanitize-recover=all','-Itests/loxone_stubs','-Itests/stubs','-Imain',
         'tests/test_loxone_protocol.c','main/app_loxone_model.c','main/app_loxone_home_model.c','main/app_loxone_home_runtime.c','-lmbedcrypto','-l:libcjson.so.1','-lm',
         '-Wl,--gc-sections','-o',tmp+'/test']
    include=os.environ.get('LOXONE_MBEDTLS_INCLUDE');lib=os.environ.get('LOXONE_MBEDTLS_LIB')
    if include:cmd[1:1]=['-I'+include]
    if lib:cmd[1:1]=['-L'+lib]
    subprocess.run(cmd,cwd=root,check=True)
    subprocess.run([tmp+'/test',tmp+'/private.pem',tmp+'/public.pem'],check=True,timeout=15)
