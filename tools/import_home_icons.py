#!/usr/bin/env python3
"""A generált otthonikonok kizárólag 96×96 RGBA runtime-változatát importálja.

A nagy eredetik a generátor kimeneti helyén maradnak. Nincs átfestés vagy
háttéreltávolítás: csak a felhasználó által kért méretre alakítás történik.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "assets/loxone-home/catalog.json"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def import_icons(manifest):
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    by_id = {entry["id"]: entry for entry in catalog["icons"]}
    folder = CATALOG.parent / "icons"
    folder.mkdir(parents=True, exist_ok=True)
    for item in manifest:
        ident = item["id"]
        if not re.fullmatch(r"[a-z0-9_-]{1,32}", ident) or ident not in by_id:
            raise ValueError(f"Ismeretlen ikonkulcs: {ident}")
        source = Path(item["path"])
        destination = folder / f"{ident}.png"
        with Image.open(source) as original:
            if original.mode != "RGBA" or original.getextrema()[3][0] != 0:
                raise ValueError(f"Valódi átlátszó RGBA forrás szükséges: {ident}")
            if original.getextrema()[3][1] < 200:
                raise ValueError(f"Üres vagy túl áttetsző ikon: {ident}")
            icon = ImageOps.contain(original, (96, 96), Image.Resampling.LANCZOS)
            output = Image.new("RGBA", (96, 96))
            output.paste(icon, ((96 - icon.width) // 2, (96 - icon.height) // 2))
            output.save(destination, optimize=True)
        by_id[ident].update({
            "file": f"icons/{ident}.png",
            "generator": "built-in image_gen",
            "prompt": item["prompt"],
            "original_sha256": digest(source),
            "sha256": digest(destination),
        })
        print(f"{ident}: {destination.stat().st_size} bájt, 96×96 RGBA")
    CATALOG.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def validate(require_complete=False):
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    entries = catalog["icons"]
    if catalog["schema"] != 1 or catalog["icon_size"] != 96:
        raise ValueError("Hibás katalógusverzió vagy ikonméret")
    if len({item["id"] for item in entries}) != len(entries):
        raise ValueError("Ismétlődő ikonkulcs")
    ready = 0
    for item in entries:
        if not re.fullmatch(r"[a-z0-9_-]{1,32}", item["id"]):
            raise ValueError("Hibás ikonkulcs")
        if not item.get("file"):
            if require_complete:
                raise ValueError(f"Hiányzó ikon: {item['id']}")
            continue
        if item["file"] != f"icons/{item['id']}.png":
            raise ValueError("Nem biztonságos ikonútvonal")
        path = CATALOG.parent / item["file"]
        with Image.open(path) as image:
            if (image.size != (96, 96) or image.mode != "RGBA" or
                    image.getextrema()[3][0] != 0 or image.getextrema()[3][1] < 200):
                raise ValueError(f"Hibás runtime PNG: {item['id']}")
        if digest(path) != item["sha256"]:
            raise ValueError(f"Eltérő ellenőrzőösszeg: {item['id']}")
        ready += 1
    expected = {item["file"] for item in entries if item.get("file")}
    actual = {f"icons/{path.name}" for path in (CATALOG.parent / "icons").glob("*")}
    if actual != expected:
        raise ValueError("Felesleges vagy hiányzó ikonfájl")
    print(f"Otthonikonok: {ready}/{len(entries)} kész; minden kész PNG 96×96, RGBA és ellenőrzött.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", nargs="?", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--complete", action="store_true")
    args = parser.parse_args()
    if args.manifest:
        import_icons(json.loads(args.manifest.read_text(encoding="utf-8")))
    if args.check or args.complete:
        validate(args.complete)
    if not (args.manifest or args.check or args.complete):
        parser.error("Adj meg importjegyzéket vagy --check/--complete kapcsolót.")
