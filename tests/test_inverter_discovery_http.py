"""A Fronius, Solplanet és Victron felderítés futtatása offline protokollmintákkal."""
import ctypes as ct
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]


class Result(ct.Structure):
    _fields_ = [(field, ct.c_char * n) for field, n in
                (("host", 16), ("model", 64), ("serial", 64), ("device", 96))]
    _fields_ += [("port", ct.c_uint16), ("unit", ct.c_uint8), ("verified", ct.c_bool)]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        self.server.requests.append((self.path, set(self.headers.keys())))
        status, body = self.server.fixtures.get(self.path, (404, {}))
        if isinstance(body, bytes):
            self.connection.sendall(body)
            self.close_connection = True
            return
        if body is None:
            time.sleep(0.4)
            return
        encoded = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for chunk in (encoded[:3], encoded[3:]):
            if chunk:
                self.wfile.write(b"%x\r\n" % len(chunk) + chunk + b"\r\n")
        self.wfile.write(b"0\r\n\r\n")


def scenarios(lib):
    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as http:
        port = http.server_address[1]
        thread = threading.Thread(target=http.serve_forever, daemon=True)
        thread.start()

        def probe(profile, fixtures, capacity=8, timeout=1000, mismatch=False):
            http.fixtures = fixtures
            http.requests = []
            out = (Result * capacity)()
            out[0].host = b"127.0.0.2" if mismatch else b"127.0.0.1"
            out[0].port = port
            client = socket.create_connection(http.server_address)
            client.setblocking(False)
            count = lib.probe(profile.encode(), client.detach(), timeout, out, capacity)
            assert all(names <= {"Host", "Accept", "Connection"} for _, names in http.requests)
            return count, out, [path for path, _ in http.requests]

        info = "/solar_api/v1/GetInverterInfo.cgi"
        flow = "/solar_api/v1/GetPowerFlowRealtimeData.fcgi"
        head = {"Status": {"Code": 0}}
        fronius = {info: (200, {"Head": head, "Body": {"Data": {
            "1": {"DT": 1, "UniqueID": "SN123"}, "2": {"DT": 1, "UniqueID": "SN234"},
            "3": {"DT": 108, "UniqueID": "OTHER"}}}}),
            flow: (200, {"Head": head, "Body": {"Data": {
                "Site": {"P_PV": 0}, "Inverters": {"1": {}, "2": {}}}}})}
        n, rows, paths = probe("fronius_http", fronius)
        assert n == 2 and paths == [info, flow]
        assert rows[0].serial == b"SN123" and rows[0].device == b"1" and rows[0].verified
        assert rows[1].serial == b"SN234" and rows[1].device == b"2" and rows[1].verified
        assert probe("fronius_http", fronius, capacity=1)[0] == 2
        for invalid_pv in (None, 327671):
            fronius[flow][1]["Body"]["Data"]["Site"]["P_PV"] = invalid_pv
            assert not probe("fronius_http", fronius)[1][0].verified
        ambiguous = b'{"Head":{"Status":{"Code":0}},"Body":{"Data":{"1":{"DT":1,"UniqueID":"A"},"1":{"DT":1,"UniqueID":"B"}}}}'
        raw = b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n" % len(ambiguous) + ambiguous
        assert probe("fronius_http", {info: (200, raw)})[0] == 0
        for invalid in [b"HTTP/1.1 200 OK\r\nContent-Length: 900\r\n\r\n{}", b"HTTP/1.1 200 OK\r\n\r\n"+b"["*100+b"]"*100,
                        b"HTTP/1.1 302 Found\r\nLocation: http://elsewhere.test/\r\nContent-Length: 0\r\n\r\n",
                        b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\n{}!\r\n",
                        b"HTTP/1.1 200 OK\r\n\r\n" + b" " * 12289]:
            n, _, paths = probe("fronius_http", {info: (200, invalid)})
            assert n == 0 and paths == [info]

        v2, v1 = "/getdev.cgi?device=2", "/invinfo.cgi"
        fleet = {"inv": [{"isn": "SER1", "model": "ASW 8000-T"}, {"isn": "SER2", "model": "ASW8000-T"},
                         {"isn": "OTHER", "model": "ASW10kH-T3"}, {"isn": "BAD/SN", "model": "ASW8000-T"},
                         {"isn": "LEGACY", "mty": 11}], "num": 5}
        for legacy, metadata, path in [(False, v2, "/getdevdata.cgi?device=2&sn="),
                                       (True, v1, "/invdata.cgi?sn=")]:
            n, rows, paths = probe("solplanet_http", {
                metadata: (200, fleet), path + "SER1": (200, {"pac": 0}),
                path + "SER2": (200, {"pac": None})})
            assert n == 3 and paths == ([v2, v1] if legacy else [v2]) + [path+"SER1", path+"SER2"]
            assert rows[0].device == rows[0].serial == b"SER1" and rows[0].verified
            assert rows[1].device == b"SER2" and not rows[1].verified
            assert rows[2].device == b"LEGACY" and not rows[2].verified
        for sample, verified in [({"pac": 65535}, False), ({"pac": 100, "isn": "SER2"}, False),
                                 ({"pac": 50, "sn": None}, False), ({"vpv": [3300], "ipv": [200]}, True)]:
            data = {v2: (200, {"inv": fleet["inv"][:1], "num": 1}),
                    "/getdevdata.cgi?device=2&sn=SER1": (200, sample)}
            n, rows, _ = probe("solplanet_http", data)
            assert n == 1 and rows[0].verified == verified
        n, _, paths = probe("solplanet_http", {v2: (200, {"inv": [fleet["inv"][0]], "num": 0})})
        assert not n and paths == [v2]
        for profile in ("solplanet_https", "solax_http", "solax_cloud", "victron_mqtt"):
            n, _, paths = probe(profile, {})
            assert not n and not paths
        n, _, paths = probe("fronius_http", fronius, mismatch=True)
        assert not n and not paths
        start = time.monotonic()
        assert probe("fronius_http", {info: (200, None)}, timeout=50)[0] == 0
        assert time.monotonic() - start < 0.3
        http.shutdown()
        thread.join()

    out = (Result * 8)()
    assert lib.native(b"fronius_http", 0, out, 8) == 2
    assert [r.host for r in out[:2]] == [b"127.0.0.2", b"127.0.0.3"]
    assert all(not r.verified and not r.device and r.serial == b"S123" for r in out[:2])
    assert lib.native(b"fronius_http", 0, out, 1) == 2

    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    with udp:
        udp.bind(("127.0.0.1", 0))
        udp.settimeout(3)

        def reply():
            request, source = udp.recvfrom(1024)
            assert b"ST: upnp:rootdevice\r\n" in request
            usn = b"uuid:com.victronenergy.ccgx.AB:12:CD:34:EF:56::upnp:rootdevice"
            base = b"HTTP/1.1 200 OK\r\nST: upnp:rootdevice\r\nLOCATION: http://elsewhere.test/\r\nUSN: "
            for value in (usn.replace(b"victronenergy", b"printerbrand"), usn + b"X",
                          usn.replace(b":34:", b":0p:"), usn, usn):
                udp.sendto(base + value + b"\r\n\r\n", source)

        for profile in (b"victron_mqtt", b"victron_modbus"):
            thread = threading.Thread(target=reply)
            thread.start()
            out = (Result * 8)()
            n = lib.native(profile, udp.getsockname()[1], out, 8)
            thread.join()
            assert n == 1 and not out[0].verified and out[0].host == b"127.0.0.1"
            assert out[0].model == b"Victron GX" and not out[0].serial
            assert out[0].device == (b"ab12cd34ef56" if profile == b"victron_mqtt" else b"")
        assert lib.native(b"solax_cloud", 0, (Result * 8)(), 8) == 0


with tempfile.TemporaryDirectory(prefix="inverter-discovery-http-") as directory:
    output = Path(directory) / "probe.so"
    # A CI a disztribúció parserét, SDK-val építve az IDF pontos forrását használja.
    parser = os.environ.get("INVERTER_HTTP_PARSER_DIR")
    parser_args = (["-I" + parser, str(Path(parser) / "http_parser.c")]
                   if parser else ["-lhttp_parser"])
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-D_GNU_SOURCE", "-O1",
                    "-fPIC", "-shared", "-fvisibility=hidden", "-ffunction-sections",
                    "-Wall", "-Wextra", "-Werror", "-Itests/inverter_stubs", "-Itests/ota_stubs",
                    "-Itests/stubs", "-Imain", "tests/test_inverter_discovery_http.c",
                    "main/app_inverter_discovery_http.c", "main/app_inverter_net.c",
                    "main/app_inverter_http.c", "main/app_inverter_http_local.c",
                    *parser_args, "-Wl,--gc-sections,--wrap=sendto,-z,defs",
                    "-l:libcjson.so.1", "-o", output], check=True, cwd=ROOT)
    library = ct.CDLL(str(output))
    library.probe.argtypes = [ct.c_char_p, ct.c_int, ct.c_uint32, ct.POINTER(Result), ct.c_size_t]
    library.probe.restype = ct.c_size_t
    library.native.argtypes = [ct.c_char_p, ct.c_uint16, ct.POINTER(Result), ct.c_size_t]
    library.native.restype = ct.c_size_t
    scenarios(library)
print("PASS: Fronius, Solplanet V1/V2, GX SSDP local discovery, timeout and credential isolation")
