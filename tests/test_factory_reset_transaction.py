"""A valódi törlési napló, újraindítási szakaszok és kártyaazonosság."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    binary = Path(tmp) / "factory_transaction"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all", "-Imain", "-Itests/stubs",
                    "-Itests/ble_profile_stubs", "main/app_factory_reset.c",
                    "tests/test_factory_reset_transaction.c", "-o", binary], cwd=ROOT, check=True)
    subprocess.run([binary], check=True, timeout=10)
