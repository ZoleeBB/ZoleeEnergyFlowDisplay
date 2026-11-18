"""A valódi C adapterek futtatása rövid, helyben visszajátszott HTTP-válaszokkal."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="inverter-http-") as tmp:
    output = Path(tmp) / "test"
    cmd = [os.environ.get("CC", "cc"), "-std=gnu11", "-D_GNU_SOURCE", "-O1",
           "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
           "-Itests/stubs", "-Imain", "tests/test_inverter_http.c", "main/app_inverter_http.c",
           "main/app_inverter_http_local.c", "main/app_inverter_http_cloud.c",
           "-l:libcjson.so.1", "-lmbedcrypto", "-Wl,--wrap=time", "-lm", "-o", output]
    for flag, variable in (("-I", "LOXONE_MBEDTLS_INCLUDE"), ("-L", "LOXONE_MBEDTLS_LIB")):
        if os.environ.get(variable):
            cmd.insert(1, flag + os.environ[variable])
    subprocess.run(cmd, cwd=root, check=True)
    subprocess.run([output], cwd=root, check=True, timeout=10)
