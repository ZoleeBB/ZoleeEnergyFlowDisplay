#!/usr/bin/env python3
"""Package checked-in template JSON, artwork and LVGL previews without rewriting sources."""
import json
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / 'templates'


def _referenced_pngs(document):
    """Collect image layers for all widget types, including solar_disc."""
    result = set()
    for variant in ('light', 'dark'):
        scene = document[variant]
        for element in scene['elements']:
            file = element.get('file')
            if file and file.endswith('.png'):
                result.add(file)
            for file in element.get('files', {}).values():
                if file.endswith('.png'):
                    result.add(file)
    return result


def package_existing_template(folder, output):
    """Package an already authored template without overwriting its JSON."""
    document = json.loads((folder/'template.json').read_text(encoding='utf-8'))
    if document.get('schema') != 1 or document.get('id') != folder.name:
        raise ValueError(f'{folder.name}: invalid id or schema')
    refs = _referenced_pngs(document) | {'preview-light.png', 'preview-dark.png'}
    present = {file.name for file in folder.glob('*.png')}
    if refs != present:
        raise ValueError(f'{folder.name}: missing PNGs {sorted(refs-present)}; unused PNGs {sorted(present-refs)}')
    with zipfile.ZipFile(output/(folder.name+'.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(folder.iterdir()):
            # A .raw segédfájlok az eszközön, telepítéskor készülnek el.
            if file.is_file() and file.suffix != '.raw':
                archive.write(file, file.name)
    print(folder.name, 'light/dark:',
          len(document['light']['elements']), len(document['dark']['elements']))



def main():
    output=ROOT/'dist/templates'
    output.mkdir(parents=True,exist_ok=True)
    folders=sorted(p for p in PACKAGES.iterdir()
                   if p.is_dir() and (p/'template.json').is_file())
    if not folders:
        raise ValueError('No templates found to package')
    for old_zip in output.glob('*.zip'):
        old_zip.unlink()  # Never ship a ZIP from an earlier build.
    for folder in folders:
        package_existing_template(folder, output)
    from prepare_home_resources import package_home_resources
    package_home_resources(output)
    assert {p.stem for p in output.glob('*.zip')} == {p.name for p in folders} | {'loxone-home'}


if __name__=='__main__':main()
