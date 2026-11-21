#!/usr/bin/env python3
"""Typed identity confirmation through the same read-only frames as polling."""
import concurrent.futures
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile

from test_inverter_modbus import RegisterImage, ROOT, serve


def packed(value, words):
    return list(struct.unpack(">" + "H" * words, value.encode("ascii").ljust(2 * words, b"\0")))


def run(exe, profile, image, model="", serial="", *, verified=False, unit=1, device="", frame="tcp"):
    with socket.socket() as listener, concurrent.futures.ThreadPoolExecutor() as pool:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(5)
        worker = pool.submit(serve, listener, frame, unit, RegisterImage(image), "")
        output = subprocess.check_output([exe, profile, str(listener.getsockname()[1]), str(unit), device],
                                         text=True, timeout=5).splitlines()
        assert output == [str(int(bool(model))), str(int(verified)), model, serial, device if model else ""], output
        worker.result(timeout=5)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        exe = Path(tmp) / "discover"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Werror=frame-larger-than=512", "-Itests/stubs", "-Imain",
                        "main/app_inverter_discovery_modbus.c", "main/app_inverter_modbus.c",
                        "main/app_inverter_registers.c", "main/app_inverter_modbus_wire.c",
                        "tests/test_inverter_discovery_modbus.c", "-lm", "-o", exe], check=True, cwd=ROOT)
        def huawei(model):
            return {(3, 30000): packed(model, 15) + packed("HUA1234", 10), (3, 32064): [0] * 26}
        run(exe, "huawei_modbus", huawei("SUN2000-5KTL-M1"), "SUN2000-5KTL-M1", "HUA1234", verified=True)
        run(exe, "huawei_modbus", huawei("SUN2000-20KTL-M3"))
        run(exe, "huawei_modbus", huawei("PLC\x00SUN2000-5KTL-M1"))
        info = {(3, 35003): packed("9010KETU000W0000", 8) + packed("", 5)}
        run(exe, "goodwe_modbus", info, "GoodWe ET", "9010KETU000W0000", unit=247)
        info[3, 35003] = packed("9010KETU000W0000", 8) + packed("GW10K-ET", 5)
        info[3, 35105] = [0] * 6
        run(exe, "goodwe_modbus", info, "GW10K-ET", "9010KETU000W0000", unit=247, verified=True)
        info[3, 35003] = packed("9010KETU000W0000", 8) + packed("GW10K-DT", 5)
        run(exe, "goodwe_modbus", info, unit=247)
        sh = {(4, 4989): packed("SG123456", 10) + [0xe13], (4, 5016): [0, 0]}
        run(exe, "sungrow_modbus", sh, "Sungrow SH10RT-20", "SG123456", verified=True)
        sh[4, 4989][-1] = 0xde
        run(exe, "sungrow_modbus", sh)
        for sn, variant in [("H34ABCDEFGHIJK", ""), ("H4501234567890", "X1-G4"), ("10K0A123456789", "X3-G4-PRO")]:
            info = {(3, 0): packed(sn, 7)}
            run(exe, "solax_modbus", info, "SolaX " + (variant or "X3-G4"), sn, device=variant)
            run(exe, "solax_modbus", info, device="X1-G4" if not variant else "")
        sp = {(4, 1002): packed("S1P2", 16) + packed("ASW8000-T", 8), (4, 1318): [0] * 4}
        run(exe, "solplanet_modbus", sp, "ASW8000-T", "S1P2", verified=True, unit=3)
        sun = {(3, 40000): [0x5375, 0x6e53, 1, 65] + packed("SolarEdge ", 16) + packed("SE8000", 16),
               (3, 40052): packed("SE12345", 16), (3, 40069): [103, 50], (3, 40083): [0] * 25}
        run(exe, "solaredge_modbus", sun, "SE8000", "SE12345", verified=True)
        sun[3, 40069] = [201, 100]
        run(exe, "solaredge_modbus", sun)
        sun[3, 40069] = [101, 50]
        sun[3, 40000][4:20] = packed("Another SunSpec Manufacturer", 16)
        run(exe, "solaredge_modbus", sun)
        gx = {(3, 800): packed("abc012345678", 6), (3, 808): [0] * 9}
        run(exe, "victron_modbus", gx, "Victron GX", "abc012345678", verified=True, unit=100)
        gx[3, 800] = packed("000000000000", 6)
        run(exe, "victron_modbus", gx, unit=100)
        deye = {(3, 0): [5, 1, 1] + packed("DEYE012345", 5), (3, 22): [0x0203], (3, 672): [500, 0]}
        for profile, frame in [("deye_modbus", "tcp"), ("deye_rtu", "rtu"), ("deye_lse3", "lse6"),
                               ("deye_lse3", "lse8"), ("deye_v5", "v5")]:
            run(exe, profile, deye, "Deye LV 3 fázis / 2 MPPT", "DEYE012345", frame=frame, verified=True,
                device="3631234567" if frame == "v5" else "")
        deye[3, 22] = [0x0403]
        run(exe, "deye_modbus", deye)
        sf = {(3, 0x445): packed("SP1ES110MSR283", 8), (3, 0x0586): [12, 0, 0, 18]}
        for profile, frame in [("sofar_modbus", "tcp"), ("sofar_v5", "v5")]:
            run(exe, profile, sf, "SOFAR HYD KTL-3PH", "SP1ES110MSR283", frame=frame, verified=True,
                device="3631234567" if frame == "v5" else "")
        sf[3, 0x445] = packed("SH1ES123456789AB", 8)
        sf[3, 0x470] = packed("CDEF", 2)
        run(exe, "sofar_modbus", sf, "SOFAR HYD KTL-3PH", "SH1ES123456789ABCDEF", verified=True)
        for serial in ["SP1ES120N61234", "SH3E123456789012", "ZZ1ABCDEFGHIJK"]:
            sf[3, 0x445] = packed(serial, 8)
            run(exe, "sofar_modbus", sf)
        # Egy más gyártó energiadatai vagy csupán egy Modbus-válasz nem elegendő.
        run(exe, "huawei_modbus", {(3, 32064): [0] * 26})
        run(exe, "sofar_v5", {}, device="hibás")
    print("Modbus inverter discovery tests passed")


if __name__ == "__main__":
    main()
