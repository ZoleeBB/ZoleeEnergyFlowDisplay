"""A tényleges ESP-IDF által szállított lwIP-forrásból futtatja az ARP-regressziót."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
lwip = Path(sys.argv[1]).resolve() / "src"
with tempfile.TemporaryDirectory() as work:
    elf = Path(work) / "arp"
    subprocess.run([
        "cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-ffunction-sections", "-fdata-sections", "-fsanitize=undefined",
        "-Itests/inverter_arp_port", "-I" + str(lwip/"include"), "-Imain",
        "-Itests/stubs", "-Itests/ble_profile_stubs",
        "tests/test_inverter_arp_wire.c", "main/app_inverter_neighbor.c",
        lwip/"netif/ethernet.c", lwip/"core/ipv4/etharp.c",
        "-Wl,--gc-sections", "-Wl,--wrap=etharp_input", "-o", elf
    ], cwd=root, check=True)
    subprocess.run([sys.executable, root/"tools/check_inverter_arp_link.py", "objdump", elf], check=True)
    subprocess.run([elf], check=True)
