#!/usr/bin/env python3
"""Local read-only Modbus TCP, RTU and SolarMAN V5 drivers through real sockets."""
import concurrent.futures
import json
import os
import socket
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def crc(data):
    value = 65535
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return struct.pack("<H", value)


def exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            if data:
                raise ValueError("short request")
            return None
        data.extend(chunk)
    return bytes(data)


def v5_frame(payload, seq, serial):
    header = struct.pack("<BHHHI", 0xa5, len(payload), 0x1510, seq, serial)
    frame = header + payload
    return frame + bytes([sum(frame[1:]) & 255, 0x15])


def serve(listener, frame, unit, registers, fault):
    visited = []
    with listener.accept()[0] as client:
        client.settimeout(5)
        while True:
            packet = exact(client, {"tcp": 12, "rtu": 8, "v5": 36}[frame])
            if packet is None:
                break
            if frame == "tcp":
                seq, protocol, size = struct.unpack(">HHH", packet[:6])
                assert protocol == 0 and size == 6
                request = packet[6:]
            else:
                request = packet if frame == "rtu" else packet[26:34]
                assert request[-2:] == crc(request[:-2])
                if frame == "v5":
                    start, length, control, seq, serial = struct.unpack("<BHHHI", packet[:11])
                    assert (start, length, control, serial) == (165, 23, 0x4510, 3631234567)
                    assert packet[11:26] == bytes([2]) + bytes(14)
                    assert packet[-2:] == bytes([sum(packet[1:-2]) & 255, 21])
                request = request[:-2]
            slave, function, address, count = struct.unpack(">BBHH", request)
            assert slave == unit and function in (3, 4)
            assert 0 < count <= 125
            visited.append((function, address, count))
            table = registers.get((function, address))
            if table is not None:
                assert len(table) == count, (address, count, table)
                data = bytes([slave, function, count * 2]) + struct.pack(">" + "H" * count, *table)
            else:
                data = bytes([slave, function | 128, 2])
            if frame == "tcp":
                reply = struct.pack(">HHH", seq + (fault == "sequence"), 0, len(data)) + data
            else:
                data += crc(data)
                if fault == "crc":
                    data = data[:-1] + bytes([data[-1] ^ 1])
                reply = data
                if frame == "v5":
                    payload = bytes([2, 1]) + bytes(12) + data + bytes(2)
                    reply = v5_frame(payload, seq, serial + (fault == "serial"))
                    if fault == "checksum":
                        reply = reply[:-2] + bytes([reply[-2] ^ 1, 21])
            # Három TCP-darabra bontva is egyetlen teljes Modbus-választ kell feldolgozni.
            client.sendall(reply[:2])
            client.sendall(reply[2:7])
            client.sendall(reply[7:])
            if fault:
                break
    return visited


def run(exe, profile, registers, expected, *, frame="tcp", unit=1, battery=True, device="", fault=""):
    with socket.socket() as listener, concurrent.futures.ThreadPoolExecutor() as pool:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(6)
        future = pool.submit(serve, listener, frame, unit, registers, fault)
        cmd = [str(exe), profile, str(listener.getsockname()[1]), str(unit), str(int(battery)), device]
        result = json.loads(subprocess.check_output(cmd, text=True, timeout=7))
        visited = future.result(timeout=6)
    assert result == expected, (profile, result, expected)
    return visited


def padded(length, values):
    data = [0] * length
    for index, value in values.items():
        data[index] = value & 65535
    return data


def main():
    with tempfile.TemporaryDirectory() as temp:
        exe = Path(temp) / "reader"
        subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        "-Werror=frame-larger-than=512", "-Itests/stubs", "-Imain",
                        "main/app_inverter_modbus_wire.c", "main/app_inverter_modbus.c",
                        "tests/test_inverter_modbus.c", "-lm", "-o", str(exe)], cwd=ROOT, check=True)
        pv = {"pv": 3.5, "status": 1}
        core = padded(26, {1: 3500, 17: 3000, 25: 0x0200})
        huawei = {(3, 32064): core, (3, 37100): padded(15, {0: 1, 14: 500}),
                  (3, 37760): [650, 0, 2, 0, 0, 0, 400], (3, 37022): [0]}
        run(exe, "huawei_modbus", huawei, dict(pv, grid=-.5, home=2.5, soc=65, battery=-.4, temp=0))
        run(exe, "huawei_modbus", {(3, 32064): core}, pv, unit=0)
        visited = run(exe, "huawei_modbus", huawei, dict(pv, grid=-.5, home=2.5), battery=False)
        assert all(a < 37000 or a == 37100 for _, a, _ in visited)
        run(exe, "huawei_modbus", huawei, None, fault="sequence")
        goodwe = {(3, 35105): padded(14, {1: 2000, 5: 1500, 8: -1, 9: -1, 12: -1, 13: -1}),
                  (3, 36004): [1], (3, 35138): [2600, 0, 1000], (3, 35182): [65535, 65036, 2],
                  (3, 37003): [0x8000, 0, 0, 0, 38]}
        run(exe, "goodwe_modbus", goodwe, dict(pv, grid=-1, home=1.6, battery=-.5, soc=38), unit=247)
        sungrow = {(4, 5016): [3500, 0], (4, 12999): padded(12, {0: 0x1000, 1: 4, 8: 1100, 10: -100, 11: -1}),
                   (4, 13019): [4820, 0, 100, 553, 0, 320]}
        run(exe, "sungrow_modbus", sungrow, dict(pv, home=1.1, grid=.1, battery=.1, soc=55.3, temp=32, status=3))
        solax = {(4, 9): [2, 1500, 2000], (4, 0x46): [500, 0], (4, 0x16): [300, 1, 22, 1, 0, 2, 50],
                 (4, 0x6c): padded(9, {0: 1000, 4: 900, 8: 700}), (4, 2): [2400]}
        run(exe, "solax_modbus", solax, dict(pv, grid=-.5, home=2.1, battery=-.3, soc=50, temp=22))
        run(exe, "solax_modbus", solax, dict(pv, grid=-.5, home=1.9), device="X1-G4", battery=False)
        victron = {(3, 808): [2000] + [0] * 8, (3, 850): [1500], (3, 820): [65036, 400, 100],
                   (3, 817): [400, 500, 200], (3, 840): [482, 0, 200, 52]}
        run(exe, "victron_modbus", victron, dict(pv, home=1.1, grid=0, battery=-.2, soc=52), unit=100)
        deye = {(3, 672): [2000, 1500], (3, 619): [65036], (3, 653): [1500],
                (3, 500): [2], (3, 586): [1220, 4800, 35, 0, 250]}
        for profile, frame in [("deye_modbus", "tcp"), ("deye_v5", "v5"), ("deye_rtu", "rtu")]:
            run(exe, profile, deye, dict(pv, grid=-.5, home=1.5, battery=.25, soc=35, temp=22),
                frame=frame, device="3631234567")
        for fault in ("serial", "crc", "checksum"):
            run(exe, "deye_v5", deye, None, frame="v5", device="3631234567", fault=fault)
        run(exe, "deye_rtu", deye, None, frame="rtu", fault="crc")
        run(exe, "solplanet_modbus", {(4, 1318): [3500, 700, 3000, 350]}, pv, unit=3, battery=False)
        sunspec = {(3, 40000): [0x5375, 0x6e53], (3, 40002): [1, 65], (3, 40069): [103, 50],
                   (3, 40083): padded(25, {0: 30, 1: 2, 17: 35, 18: 2, 24: 4}),
                   (3, 40121): [1, 65], (3, 40155): list(struct.unpack(">8H", b"Export+Import".ljust(16, b"\0"))),
                   (3, 40188): [203, 105], (3, 40206): [5, 0, 0, 0, 2], (3, 40295): [65535, 0]}
        run(exe, "solaredge_modbus", sunspec, dict(pv, grid=-.5, home=2.5), battery=False)
        battery_words = [0] * 28
        for offset, value in [(0, 0), (8, 750), (24, 55)]:
            high, low = struct.unpack(">HH", struct.pack(">f", value))
            battery_words[offset:offset+2] = [low, high]
        battery_words[26] = 3
        sunspec[3, 0xe16c] = battery_words
        run(exe, "solaredge_modbus", sunspec, dict(pv, pv=4.25, grid=-.5, home=2.5, battery=-.75, soc=55, temp=0))
    print("Local inverter profiles and fragmented Modbus TCP/RTU/SolarMAN V5 frames passed.")


if __name__ == "__main__":
    main()
