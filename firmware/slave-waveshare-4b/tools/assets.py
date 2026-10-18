"""PNG-forrásokból közös, hordozható LVGL-erőforrás; nincs képmódosítás."""
import pathlib
import struct
import sys

root = pathlib.Path(__file__).resolve().parents[1]
project = root.parents[1]
assets = {"day": root / "assets/day.png", "night": root / "assets/night.png"}
for name in ("hub", "weather_sun", "weather_cloud", "weather_partly", "weather_rain",
             "weather_snow", "weather_moon", "wind"):
    stem = "flow_hub" if name == "hub" else name
    assets[name] = project / "main/assets/png" / (stem + ".png")
output = pathlib.Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
with output.open("w") as target:
    target.write('/* Generált fájl: tools/assets.py */\n#include "lvgl.h"\n')
    for name, source in assets.items():
        data = source.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", source
        width, height = struct.unpack(">II", data[16:24])
        target.write(f"static const uint8_t data_{name}[] = {{\n")
        for start in range(0, len(data), 24):
            target.write(",".join(f"0x{byte:02x}" for byte in data[start:start + 24]) + ",\n")
        target.write("};\n")
        target.write(f"const lv_image_dsc_t slave_4b_{name} = {{.header = {{"
                     f".magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW_ALPHA, "
                     f".w = {width}, .h = {height}}}, .data_size = sizeof(data_{name}), "
                     f".data = data_{name}}};\n")
