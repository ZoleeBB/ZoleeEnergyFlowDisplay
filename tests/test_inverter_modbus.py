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


class RegisterImage:
    """Word-addressable fixture; missing ranges produce a Modbus exception."""
    def __init__(self, blocks=()):
        self.words = {}
        for (function, address), values in dict(blocks).items():
            self.put(function, address, values)

    def put(self, function, address, values):
        for offset, value in enumerate(values):
            self.words[function, address + offset] = value

    def read(self, function, address, count):
        try:
            return [self.words[function, addr] for addr in range(address, address + count)]
        except KeyError:
            return None


def schema(exe, profile):
    rows = json.loads(subprocess.check_output([str(exe), "--schema", profile], text=True))
    assert len(rows) <= 32
    assert len({row["key"] for row in rows}) == len(rows)
    assert all(row["fc"] in (3, 4) and 1 <= row["words"] <= 8 and
               0 <= row["address"] <= 65536 - row["words"] for row in rows)
    return rows


def relocated(exe, profile, blocks, expected, *, frame="tcp", unit=1, device="", battery=True):
    fields = schema(exe, profile)
    source = RegisterImage(blocks)
    image = RegisterImage()
    overrides = {}
    required = set()
    for i, field in enumerate(fields):
        assert not field["base"]
        address = 20000 + 12 * i
        values = source.read(field["fc"], field["address"], field["words"])
        overrides[field["key"]] = address
        if values is not None:
            image.put(field["fc"], address, values)
            required.add((field["fc"], address, field["words"]))
    seen = run(exe, profile, image, expected, device=device, unit=unit, battery=battery,
               frame=frame, overrides=overrides)
    # A feltételes útvonalak (eltérő modell, tartalék BMS) közül csak az aktív olvasható.
    assert seen and all(a in overrides.values() for _, a, _ in seen)
    assert set(seen) & required
    return seen


def verify_relative_registers(exe, original, pv):
    # A fejléc és a teljes modelllánc új bázist kap, benne a DC és a mérő teljesítmény szava eltér.
    image = RegisterImage()
    moved_base = 12000
    shift = moved_base - 40000
    for (fc, addr), values in original.items():
        if 40000 <= addr < 41000:
            image.put(fc, addr + shift, values)
    image.put(3, 40071 + shift + 42, [4321])
    image.put(3, 40071 + shift + 43, [65535])  # SF = −1
    image.put(3, 40190 + shift + 33, [20])
    for target in (61000, 62000):
        image.put(3, target + 0x42, original[3, 0xe142])
        image.put(3, target + 0x6c, original[3, 0xe16c])
        image.put(3, target + 155, [0, 0x4220])  # 40.0 °C, kis szó elöl
    changes = {"sunspec_base": moved_base, "dc": 42, "dc_sf": 43, "meter_power": 33,
               "bank1": 61000, "bank2": 62000, "bat_temp": 155}
    seen = run(exe, "solaredge_modbus", image,
               dict(pv, pv=1.9321, home=1, grid=-2, soc=55, battery=-1.5, temp=40), overrides=changes)
    assert (3, moved_base, 2) in seen and (3, 40000, 2) not in seen
    assert (3, 40071 + shift + 42, 2) in seen
    assert (3, 61000 + 155, 2) in seen and (3, 62000 + 155, 2) in seen
    # A modell hosszán kívüli cím nem érintheti a következő modellt vagy a 0. címet.
    changes["dc"] = 65535
    visits = run(exe, "solaredge_modbus", image, None, battery=False, overrides=changes)
    assert all(addr != 0 for _, addr, _ in visits)
    assert all(addr != ((40071 + shift + 65535) & 65535) for _, addr, _ in visits)


def serve(listener, frame, unit, registers, fault):
    visited = []
    if frame == "lse8":
        with listener.accept()[0] as first:
            assert exact(first, 14)[:6] == bytes.fromhex("03e800000006")
    with listener.accept()[0] as client:
        client.settimeout(5)
        while True:
            packet = exact(client, {"tcp": 12, "rtu": 8, "v5": 36, "lse6": 14, "lse8": 14}[frame])
            if packet is None:
                break
            if frame.startswith("lse"):
                seq, protocol, size = struct.unpack(">HHH", packet[:6])
                assert (seq, protocol, size) == (1000, 0, int(frame[3:]))
                request = packet[6:-2]
                assert packet[-2:] == crc(request)
            elif frame == "tcp":
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
            if isinstance(registers, RegisterImage):
                table = registers.read(function, address, count)
            else:
                table = registers.get((function, address, count), registers.get((function, address)))
            if table is not None:
                assert len(table) == count, (address, count, table)
                data = bytes([slave, function, count * 2]) + struct.pack(">" + "H" * count, *table)
            else:
                data = bytes([slave, function | 128, 2])
            if frame.startswith("lse"):
                if frame == "lse8":
                    data += crc(data)
                reply = struct.pack(">HHH", 1000, 0, len(data)) + data
            elif frame == "tcp":
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


def run(exe, profile, registers, expected, *, frame="tcp", unit=1, battery=True, device="", fault="", overrides=None):
    with socket.socket() as listener, concurrent.futures.ThreadPoolExecutor() as pool:
        listener.bind(("127.0.0.1", 0))
        listener.listen()
        listener.settimeout(6)
        future = pool.submit(serve, listener, frame, unit, registers, fault)
        cmd = [str(exe), profile, str(listener.getsockname()[1]), str(unit), str(int(battery)), device]
        for key, address in (overrides or {}).items():
            cmd.extend([key, str(address)])
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
                        "main/app_inverter_modbus_wire.c", "main/app_inverter_modbus.c", "main/app_inverter_registers.c",
                        "tests/test_inverter_modbus.c", "-lm", "-o", str(exe)], cwd=ROOT, check=True)
        pv = {"pv": 3.5, "status": 1}
        core = padded(26, {1: 3500, 17: 3000, 25: 0x0200})
        huawei = {(3, 32064): core, (3, 37100): padded(15, {0: 1, 14: 500}),
                  (3, 37760): [650, 0, 2, 0, 0, 0, 400], (3, 37022): [0]}
        run(exe, "huawei_modbus", huawei, dict(pv, grid=-.5, home=2.5, soc=65, battery=-.4, temp=0))
        relocated(exe, "huawei_modbus", huawei, dict(pv, grid=-.5, home=2.5, soc=65, battery=-.4, temp=0))
        run(exe, "huawei_modbus", {(3, 32064): core}, pv, unit=0)
        image = RegisterImage(huawei)
        image.put(3, 0, [0, 2400])
        changed = run(exe, "huawei_modbus", image, dict(pv, pv=2.4, grid=-.5, home=2.5, soc=65, battery=-.4),
                      overrides={"pv": 0, "bat_temp": 12900})
        assert (3, 0, 2) in changed and (3, 32064, 26) not in changed
        assert (3, 37100, 15) in changed   # változatlan blokk továbbra is egy kérés
        # Az első két szó áthelyezésekor a hiányzó új hőfok nem szünteti meg az energiamintát.
        assert (3, 12900, 1) in changed
        assert schema(exe, "fronius_local") == []
        visited = run(exe, "huawei_modbus", huawei, dict(pv, grid=-.5, home=2.5), battery=False)
        assert all(a < 37000 or a == 37100 for _, a, _ in visited)
        run(exe, "huawei_modbus", huawei, None, fault="sequence")
        run(exe, "huawei_modbus", {(3, 32064): padded(26, {0: 0x7fff, 1: 65535})}, None)
        goodwe = {(3, 35105): padded(14, {1: 2000, 5: 1500, 8: -1, 9: -1, 12: -1, 13: -1}),
                  (3, 36004): [1], (3, 35138): [2600, 0, 1000], (3, 35182): [65535, 65036, 2],
                  (3, 37003): [0x8000, 0, 0, 0, 38]}
        run(exe, "goodwe_modbus", goodwe, dict(pv, grid=-1, home=1.6, battery=-.5, soc=38), unit=247)
        relocated(exe, "goodwe_modbus", goodwe, dict(pv, grid=-1, home=1.6, battery=-.5, soc=38), unit=247)
        eh = {(3, 35105, 6): [0, 1800, 0, 0, 0, 1700]}
        run(exe, "goodwe_modbus", eh, pv, unit=247, battery=False)
        sungrow = {(4, 5016): [3500, 0], (4, 12999): padded(12, {0: 0x1000, 1: 4, 8: 1100, 10: -100, 11: -1}),
                   (4, 13019): [4820, 0, 100, 553, 0, 320]}
        run(exe, "sungrow_modbus", sungrow, dict(pv, home=1.1, grid=.1, battery=.1, soc=55.3, temp=32, status=3))
        relocated(exe, "sungrow_modbus", sungrow, dict(pv, home=1.1, grid=.1, battery=.1, soc=55.3, temp=32, status=3))
        sungrow[4, 13049] = [0, 1 << 14]
        run(exe, "sungrow_modbus", sungrow, dict(pv, status=3), battery=False)
        solax = {(4, 0xb8): [1], (4, 9): [2, 1500, 2000], (4, 0x46): [500, 0], (4, 0x16): [300, 1, 22, 1, 0, 2, 50],
                 (4, 0x6c): padded(9, {0: 1000, 4: 900, 8: 700}), (4, 2): [2400]}
        run(exe, "solax_modbus", solax, dict(pv, grid=-.5, home=2.1, battery=-.3, soc=50, temp=22))
        run(exe, "solax_modbus", solax, dict(pv, grid=-.5, home=1.9), device="X1-G4", battery=False)
        for version, third in [(98, 0x2c), (100, 0x124)]:
            pro = dict(solax)
            pro.update({(3, 0x82): [version], (4, 0x1b): [3], (4, third): [930],
                        (4, 0x16): [300, 0, 18, 0, 0, 3, 40],
                        (4, 0x38): [300, 0, 0, 0, 40]})
            run(exe, "solax_modbus", pro, dict(pv, pv=4.43, grid=-.5, home=2.1, battery=-.3, soc=40, temp=18),
                device="X3-G4-PRO")
            relocated(exe, "solax_modbus", pro,
                      dict(pv, pv=4.43, grid=-.5, home=2.1, battery=-.3, soc=40, temp=18), device="X3-G4-PRO")
            del pro[4, third]
            run(exe, "solax_modbus", pro, None, device="X3-G4-PRO")
        victron = {(3, 808): [2000] + [0] * 8, (3, 850): [1500], (3, 820): [65036, 400, 100],
                   (3, 817): [400, 500, 200], (3, 840): [482, 0, 200, 52]}
        run(exe, "victron_modbus", victron, dict(pv, home=1.1, grid=0, battery=-.2, soc=52), unit=100)
        relocated(exe, "victron_modbus", victron, dict(pv, home=1.1, grid=0, battery=-.2, soc=52), unit=100)
        deye = {(3, 672): [2000, 1500], (3, 619): [65036], (3, 653): [1500],
                (3, 500): [2], (3, 586): [1220, 4800, 35, 0, 250]}
        for profile, frame in [("deye_modbus", "tcp"), ("deye_v5", "v5"), ("deye_rtu", "rtu"),
                               ("deye_lse3", "lse6"), ("deye_lse3", "lse8")]:
            run(exe, profile, deye, dict(pv, grid=-.5, home=1.5, battery=.25, soc=35, temp=22),
                frame=frame, device="3631234567")
            relocated(exe, profile, deye, dict(pv, grid=-.5, home=1.5, battery=.25, soc=35, temp=22),
                      frame=frame, device="3631234567")
        for fault in ("serial", "crc", "checksum"):
            run(exe, "deye_v5", deye, None, frame="v5", device="3631234567", fault=fault)
        run(exe, "deye_rtu", deye, None, frame="rtu", fault="crc")
        run(exe, "solplanet_modbus", {(4, 1318): [3500, 700, 3000, 350]}, pv, unit=3, battery=False)
        relocated(exe, "solplanet_modbus", {(4, 1318): [3500, 700, 3000, 350]}, pv, unit=3, battery=False)
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
        sunspec[3, 0xe142] = [0, 0x4500]
        sunspec[3, 0xe16c] = battery_words
        run(exe, "solaredge_modbus", sunspec, dict(pv, pv=4.25, grid=-.5, home=2.5, battery=-.75, soc=55, temp=0))
        sunspec[3, 0xe242] = [0, 0x4500]
        second = battery_words.copy()
        second[26] = 4
        second[25] = struct.unpack(">HH", struct.pack(">f", 35))[0]
        sunspec[3, 0xe26c] = second
        run(exe, "solaredge_modbus", sunspec, dict(pv, grid=-.5, home=2.5, battery=0, soc=45, temp=0))
        verify_relative_registers(exe, sunspec, pv)
    print("Local inverter profiles and fragmented Modbus TCP/RTU/SolarMAN V5 frames passed.")


if __name__ == "__main__":
    main()
