"""Deterministic release media, generated from the same checkout as the firmware."""
import io
import sys
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_home_resources import home_resource_files
from prepare_ringtones import package_ringtones


def archive_bytes(files):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    return buffer.getvalue()


def package_media(output, tag):
    files = {}
    for folder in sorted((ROOT / 'templates').iterdir()):
        if folder.is_dir() and (folder / 'template.json').is_file():
            files[folder.name + '.zip'] = archive_bytes({p.name: p.read_bytes() for p in folder.iterdir() if p.is_file()})
    files['loxone-home.zip'] = archive_bytes(home_resource_files())
    templates = output / f'ZoleeEnergyFlowDisplay-{tag}-templates.zip'
    templates.write_bytes(archive_bytes(files))
    ringtones = output / f'ZoleeEnergyFlowDisplay-{tag}-ringtones.zip'
    package_ringtones(ringtones)
    return [templates, ringtones]
