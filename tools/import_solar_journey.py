#!/usr/bin/env python3
"""Import generated Nap pálya PNGs into a local repository checkout (no commit)."""
import hashlib
import json
import sys
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / "templates/09-solar-journey"
MANIFEST = ROOT / "assets/template-manifest.json"
FILES = set("day.png night.png pv.png home.png grid.png battery.png eco_leaf.png weather_sunny.png weather_partly_cloudy.png weather_rain.png weather_snow.png weather_moon.png weather_wind.png".split())

def references(value):
    if isinstance(value, dict):
        for child in value.values():
            yield from references(child)
    elif isinstance(value, list):
        for child in value:
            yield from references(child)
    elif isinstance(value, str) and value.endswith(".png"):
        yield value

def main(archive):
    with ZipFile(archive) as z:
        if set(z.namelist()) != FILES | {"template.json"}:
            raise ValueError("Unexpected or missing asset files")
        for info in z.infolist():
            if "/" in info.filename or info.file_size > 2*1024*1024:
                raise ValueError("Unsafe ZIP entry: " + info.filename)
        doc = json.loads(z.read("template.json"))
        if doc.get("schema") != 1 or doc.get("id") != "09-solar-journey" or doc.get("version", 0) < 2:
            raise ValueError("Unsupported template")
        if not set(references(doc)).issubset(FILES):
            raise ValueError("Missing referenced images")
        TARGET.mkdir(parents=True, exist_ok=True)
        for name in FILES:
            (TARGET / name).write_bytes(z.read(name))
        (TARGET / "template.json").write_text(json.dumps(doc, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    manifest = [x for x in manifest if not x["asset"].startswith("templates/09-solar-journey/")]
    manifest.extend({"asset": "templates/09-solar-journey/"+name,
                     "sha256": hashlib.sha256((TARGET / name).read_bytes()).hexdigest()}
                    for name in sorted(FILES))
    manifest.sort(key=lambda x: x["asset"])
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
    print(f"Imported {len(FILES)} regenerated images and v{doc['version']} template")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: python tools/import_solar_journey.py <NapPalya-komplett-sablon.zip>")
    main(Path(sys.argv[1]))
