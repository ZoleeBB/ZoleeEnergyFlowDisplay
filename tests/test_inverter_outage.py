#!/usr/bin/env python3
"""A valódi Master ütemező és Modbus TCP adatútja áramtalanított, majd új címen induló inverterrel."""
import concurrent.futures
import contextlib
import os
import select
import socket
import subprocess
import tempfile
import threading
from pathlib import Path

from test_inverter_modbus import padded, serve

ROOT = Path(__file__).resolve().parents[1]


def exercise():
    with tempfile.TemporaryDirectory() as name, contextlib.ExitStack() as sockets:
        directory = Path(name)
        executable = directory / "outage"
        flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                 "-Itests/inverter_stubs", "-Itests/stubs", "-Itests/ble_profile_stubs", "-Imain"]
        sources = ["app_state", "app_inverter_settings", "app_inverter_identity", "app_inverter",
                   "app_inverter_registers", "app_inverter_modbus_wire", "app_inverter_modbus"]
        subprocess.run([os.environ.get("CC", "cc"), *flags, *(f"main/{s}.c" for s in sources),
                        "tests/test_inverter_outage.c", "-Wl,--wrap=time", "-Wl,--wrap=connect",
                        "-l:libcjson.so.1", "-lm", "-pthread", "-o", executable], cwd=ROOT, check=True)
        listeners = []
        for label in ("old", "off", "foreign", "new"):
            sock = sockets.enter_context(socket.socket())
            sock.bind(("127.0.0.1", 0))
            if label != "off":
                sock.listen()
                sock.settimeout(8)
            listeners.append(sock)
        old, _, foreign, new = listeners
        ports = [str(s.getsockname()[1]) for s in listeners]

        def run(stage):
            subprocess.run([executable, stage, *ports], cwd=directory, check=True, timeout=12)

        def meter(watts):
            return {(3, 32064): padded(26, {1: watts, 17: watts, 25: 0x0200})}

        run("configure")
        persisted = (directory / "profile1").read_bytes()
        assert persisted and not (directory / "lan_pin").exists()
        finished = threading.Event()
        def serve_foreign():
            reads = []
            while not finished.is_set():
                if select.select([foreign], [], [], 0.05)[0]:
                    reads.extend(serve(foreign, "tcp", 7, meter(9750), ""))
            return reads

        with concurrent.futures.ThreadPoolExecutor() as pool:
            impostor = pool.submit(serve_foreign)
            before = pool.submit(serve, old, "tcp", 7, meter(2500), "")
            after = pool.submit(serve, new, "tcp", 7, meter(4100), "")
            try:
                run("outage")
                assert before.result() == after.result() == [(3, 32064, 26), (3, 37100, 15)]
                restarted = pool.submit(serve, new, "tcp", 7, meter(4100), "")
                run("restart")
                assert restarted.result() == after.result()
            finally:
                finished.set()
            assert not impostor.result(), "Az idegen MAC-címhez tartozó inverter Modbus-kérést kapott."
        assert (directory / "profile1").read_bytes() == persisted
        assert (directory / "lan_pin").stat().st_size
        assert not select.select([foreign], [], [], 0)[0], "Az új tulajdonos TCP-kapcsolatot kapott."
    print("PASS Master stays online, times out, finds the paired MAC, reads Modbus at the new IP and restores the pin after restart")


if __name__ == "__main__":
    exercise()
