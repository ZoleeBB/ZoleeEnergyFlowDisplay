"""GX MQTT munkamenet és mintaösszesítés gazdagépes regressziója."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    output = Path(tmp) / "gx"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    "-Itests/inverter_mqtt_stubs", "-Itests/stubs", "-Itests/ota_stubs",
                    "-Itests/ble_profile_stubs", "-Imain", "main/app_inverter_victron.c",
                    "main/app_inverter_victron_model.c", "tests/test_inverter_victron.c",
                    "-l:libcjson.so.1", "-lm", "-o", output], cwd=root, check=True)
    subprocess.run([output], cwd=root, check=True)
