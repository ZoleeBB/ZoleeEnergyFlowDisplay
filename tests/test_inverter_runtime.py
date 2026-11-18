"""Közös host regresszió a valódi beállítás-, mérés- és napszámítási úthoz."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cflags = ["-Wall", "-Wextra", "-Werror", "-g", "-fsanitize=undefined", "-Imain"]
    cpp = os.environ.get("CXX", "c++")
    cc = os.environ.get("CC", "cc")
    solar = ["main/app_inverter_solar.cpp", "main/third_party/SolarCalculator/SolarCalculator.cpp"]
    def run(args):
        subprocess.run(args, cwd=ROOT, check=True, timeout=90)
    run([cpp, *cflags, *solar, "tests/test_inverter_solar.cpp", "-o", tmp/"solar"])
    run([tmp/"solar"])
    run([cc, *cflags, "-std=gnu11", "-Itests/inverter_stubs", "-Itests/ota_stubs", "-Itests/stubs",
         "main/app_inverter_net.c", "tests/test_inverter_net.c", "-l:libcjson.so.1", "-o", tmp/"net"])
    run([tmp/"net"])
    run([cc, *cflags, "-std=gnu11", "-Itests/stubs", "-Itests/ble_profile_stubs",
         "main/app_inverter_identity.c", "tests/test_inverter_identity.c", "-o", tmp/"identity"])
    run([tmp/"identity"])
    run([cc, *cflags, "-std=gnu11", "-Itests/inverter_stubs", "-Itests/stubs", "main/app_inverter_mdns.c",
         "tests/test_inverter_mdns.c", "-o", tmp/"mdns"])
    run([tmp/"mdns"])
    objs = []
    for name in ["main/app_inverter_identity.c", "main/app_inverter_registers.c", "main/app_inverter_settings.c", "main/app_inverter_weather.c", "main/app_inverter.c",
                 "tests/test_inverter_runtime.c"]:
        obj = tmp/(Path(name).stem + ".o")
        run([cc, *cflags, "-std=gnu11", "-Itests/inverter_stubs", "-Itests/stubs",
             "-Itests/ble_profile_stubs", "-c", name, "-o", obj])
        objs.append(obj)
    run([cpp, *cflags, *objs, *solar, "-Wl,--wrap=time", "-l:libcjson.so.1", "-pthread", "-o", tmp/"runtime"])
    run([tmp/"runtime"])
    run([tmp/"runtime", "registers"])


# Telepített SDK esetén a regresszióban a valódi ethernet_input és etharp_input is részt vesz.
idf_path = os.environ.get("IDF_PATH")
if idf_path and (Path(idf_path)/"components/lwip/lwip/src/netif/ethernet.c").exists():
    subprocess.run([os.sys.executable, str(ROOT/"tests/test_inverter_arp_wire.py"),
                    str(Path(idf_path)/"components/lwip/lwip")], cwd=ROOT, check=True)
