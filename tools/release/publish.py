#!/usr/bin/env python3
"""Attach checked firmware assets without deleting or overwriting a release asset."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import urllib.parse
import urllib.request
from version import validate_tag


def api(url, data=None, content_type='application/json', method=None):
    request = urllib.request.Request(url, data=data, method=method, headers={
        'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'],
        'Accept': 'application/vnd.github+json', 'Content-Type': content_type,
        'X-GitHub-Api-Version': '2022-11-28'})
    with urllib.request.urlopen(request, timeout=120) as response:
        return json.load(response)


def publish(directory, repository, tag, commit, release_id, allow_draft=False):
    validate_tag(tag)
    root = f'https://api.github.com/repos/{repository}'
    release = api(f'{root}/releases/{release_id}')
    if release['tag_name'] != tag or (release['draft'] and not allow_draft):
        raise ValueError('Release identity changed or release is a draft')
    actual = api(f'{root}/commits/{urllib.parse.quote(tag, safe="")}')['sha']
    if actual != commit:
        raise ValueError('Release tag no longer points to the built commit')
    info = json.loads((directory / 'build-info.json').read_text())
    if info['tag'] != tag or info['commit'] != commit:
        raise ValueError('Artifact provenance does not match this release')
    expected_names = {f'ZoleeEnergyFlowDisplay-{tag}-ota.bin', f'ZoleeEnergyFlowDisplay-{tag}-usb.zip', 'build-info.json',
                      f'ZoleeEnergyFlowDisplay-{tag}-templates.zip', f'ZoleeEnergyFlowDisplay-{tag}-ringtones.zip'}
    # Két cél közös kiadásakor mindkét forrásazonosságot ellenőrizzük feltöltés előtt.
    slave_info = directory / 'slave-4b-build-info.json'
    if slave_info.exists():
        slave = json.loads(slave_info.read_text())
        if (slave['tag'] != tag or slave['commit'] != commit or
                slave['board'] != 'Waveshare ESP32-P4-WIFI6-Touch-LCD-4B'):
            raise ValueError('Slave artifact provenance does not match this release')
        expected_names.update({f'ZoleeEnergyFlowDisplay-slave-4b-{tag}-ota.bin',
                               f'ZoleeEnergyFlowDisplay-slave-4b-{tag}-usb.zip',
                               'slave-4b-build-info.json'})
    checks = {}
    for line in (directory / 'SHA256SUMS.txt').read_text().splitlines():
        digest, name = line.split('  ', 1)
        if name not in expected_names or name in checks:
            raise ValueError('Unexpected or duplicate checksum entry')
        checks[name] = digest
    if set(checks) != expected_names:
        raise ValueError('Incomplete release checksums')
    checks['SHA256SUMS.txt'] = hashlib.sha256((directory / 'SHA256SUMS.txt').read_bytes()).hexdigest()
    assets = []
    page = 1
    while True:
        batch = api(f'{root}/releases/{release_id}/assets?per_page=100&page={page}')
        assets.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    pending = []
    # Preflight every file before uploading any; a rerun must never replace bytes.
    for name, digest in checks.items():
        data = (directory / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f'Local checksum mismatch: {name}')
        matches = [asset for asset in assets if asset['name'] == name]
        if matches:
            asset = matches[0]
            if len(matches) != 1 or asset.get('state') != 'uploaded' or asset.get('digest') != 'sha256:' + digest:
                raise ValueError(f'Existing asset differs or its checksum cannot be verified: {name}. Publish a new version; nothing was overwritten.')
            print(f'Already uploaded, checksum matches: {name}')
        else:
            pending.append((name, data))
    if pending and release.get('immutable'):
        raise ValueError('This published release is immutable and cannot accept new assets. Firmware remains in the workflow artifact; repository protection was not changed.')
    for name, data in pending:
        url = release['upload_url'].split('{', 1)[0] + '?' + urllib.parse.urlencode({'name': name})
        asset = api(url, data, 'application/octet-stream')
        if asset.get('state') != 'uploaded' or asset.get('size') != len(data):
            raise ValueError(f'Upload did not complete: {name}')
        print(f'Uploaded: {name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, default=Path('dist'))
    parser.add_argument('--repository', required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--release-id', type=int, required=True)
    args = parser.parse_args()
    publish(args.directory, args.repository, args.tag, args.commit, args.release_id)
