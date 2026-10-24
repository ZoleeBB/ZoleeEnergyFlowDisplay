#!/usr/bin/env python3
"""Create one complete release per merged main PR, including scoped major promotions."""
import argparse
import json
import os
from pathlib import Path
import re
from urllib.error import HTTPError
from publish import api, publish
from version import validate_tag


def stable_version(value):
    match = re.fullmatch(r'v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', value or '')
    return tuple(map(int, match.groups())) if match else None


def next_tag(tags, releases):
    versions = [stable_version(tag['name']) for tag in tags]
    # Legacy releases may have a non-version tag (e.g. Prod), but a numeric title.
    for release in releases:
        version = stable_version(release['tag_name'])
        if version is None and not release.get('prerelease'):
            version = stable_version(release.get('name'))
        versions.append(version)
    major, minor, patch = max((v for v in versions if v is not None), default=(0, 0, 0))
    return validate_tag(f'v{major}.{minor}.{patch + 1}')


def tag_for_merged_pr(repository, number, tags, releases, merged_pr=None):
    """A jóváhagyott projektkiadások szűken célzott főverzió-emelése.

    A meglévő verzió nem írható felül; más PR továbbra is patch-verziót kap.
    """
    head = (merged_pr or {}).get('head') or {}
    if (repository == 'ZoleeBB/ZoleeEnergyFlowDisplay' and number == 20 and
            head.get('ref') == 'feature/loxone-intercom' and
            (head.get('repo') or {}).get('full_name') == repository):
        # Kizárólag a saját kaputelefonos PR egyszeri célverziója.
        versions = [stable_version(t.get('name')) for t in tags]
        versions += [stable_version(r.get('tag_name')) or stable_version(r.get('name'))
                     for r in releases if not r.get('prerelease')]
        if any(v is not None and v >= (3, 1, 0) for v in versions):
            raise ValueError('Cannot assign v3.1.0: stable v3.1.0 or newer already exists')
        return validate_tag('v3.1.0')
    if (repository == 'ZoleeBB/ZoleeEnergyFlowDisplay' and
            head.get('ref') == 'feature/v3-loxone-home' and
            (head.get('repo') or {}).get('full_name') == repository):
        # A prepare előbb igazolja az összevont main PR-t és a merge SHA-t.
        # Azonos nevű forkág vagy önmagában a PR címe nem emelhet főverziót.
        versions = [stable_version(t.get('name')) for t in tags]
        versions += [stable_version(r.get('tag_name')) or stable_version(r.get('name'))
                     for r in releases if not r.get('prerelease')]
        if any(v is not None and v >= (3, 0, 0) for v in versions):
            raise ValueError('Cannot assign v3.0.0: stable v3.0.0 or newer already exists')
        return validate_tag('v3.0.0')
    if repository == 'ZoleeBB/ZoleeEnergyFlowDisplay' and number == 12:
        versions=[stable_version(t.get('name')) for t in tags]
        versions += [stable_version(r.get('tag_name')) or stable_version(r.get('name')) for r in releases if not r.get('prerelease')]
        if any(v is not None and v >= (2,0,0) for v in versions):
            raise ValueError('Cannot assign v2.0.0: stable v2.0.0 or newer already exists')
        return validate_tag('v2.0.0')
    if repository != 'ZoleeBB/ZoleeEnergyFlowDisplay' or number != 9:
        return next_tag(tags, releases)
    versions = [stable_version(t.get('name')) for t in tags]
    for release in releases:
        version = stable_version(release.get('tag_name'))
        if version is None and not release.get('prerelease'):
            version = stable_version(release.get('name'))
        versions.append(version)
    if any(v is not None and v >= (1, 0, 0) for v in versions):
        raise ValueError('Cannot assign v1.0.0: stable v1.0.0 or newer already exists')
    return validate_tag('v1.0.0')


def listing(root, endpoint):
    result = []
    page = 1
    while True:
        batch = api(f'{root}/{endpoint}?per_page=100&page={page}')
        result.extend(batch)
        if len(batch) < 100:
            return result
        page += 1


def identity(number, commit):
    if number <= 0 or not re.fullmatch('[0-9a-f]{40}', commit):
        raise ValueError('Invalid PR number or merge commit')
    return f'<!-- zolee-auto-release:pr={number};commit={commit} -->'


def verify_merge(root, repository, number, commit):
    pr = api(f'{root}/pulls/{number}')
    if (not pr['merged'] or pr['base']['ref'] != 'main' or
            pr['base']['repo']['full_name'] != repository or pr['merge_commit_sha'] != commit):
        raise ValueError('Only the verified, merged main PR commit may be released')
    return pr


def find_release(releases, marker):
    matches = [r for r in releases if marker in (r.get('body') or '')]
    if len(matches) > 1:
        raise ValueError('Multiple releases claim the same merge; refusing to guess')
    return matches[0] if matches else None


def owned_tag(root, tag, marker, commit):
    """Read-only proof that this version was reserved by this exact merged PR."""
    try:
        ref = api(f'{root}/git/ref/tags/{tag}')
    except HTTPError as error:
        if error.code == 404:
            return False
        raise
    if ref['object']['type'] != 'tag':
        return False
    annotation = api(f'{root}/git/tags/{ref["object"]["sha"]}')
    return (annotation['message'].strip() == marker and
            annotation['object']['type'] == 'commit' and annotation['object']['sha'] == commit)


def reserve_tag(root, tag, marker, commit):
    # Only called by the existing write-authorized publication job, after the build.
    if owned_tag(root, tag, marker, commit):
        return
    annotation = api(f'{root}/git/tags', json.dumps({
        'tag': tag, 'message': marker, 'object': commit, 'type': 'commit'
    }).encode())
    try:
        api(f'{root}/git/refs', json.dumps({'ref': f'refs/tags/{tag}', 'sha': annotation['sha']}).encode())
    except HTTPError as error:
        if error.code != 422:
            raise
        if not owned_tag(root, tag, marker, commit):
            raise ValueError('Version tag belongs to another release; no tag was moved') from error


def prepare(repository, number, commit):
    root = f'https://api.github.com/repos/{repository}'
    marker = identity(number, commit)
    merged_pr = verify_merge(root, repository, number, commit)
    releases = listing(root, 'releases')
    existing = find_release(releases, marker)
    if existing:
        tag = validate_tag(existing['tag_name'])
        if existing['target_commitish'] != commit:
            raise ValueError('Existing automatic release points to a different commit')
        return {'tag': tag, 'needed': 'true' if existing['draft'] else 'false'}
    # Read-only tokens cannot see draft releases. The public annotated tag records
    # a failed publication's identity without granting preparation write access.
    tags = listing(root, 'tags')
    reserved = [tag['name'] for tag in tags
                if stable_version(tag['name']) and tag['commit']['sha'] == commit
                and owned_tag(root, tag['name'], marker, commit)]
    if len(reserved) > 1:
        raise ValueError('Multiple versions reserved for the same merge')
    return {'tag': reserved[0] if reserved else tag_for_merged_pr(repository, number, tags, releases, merged_pr),
            'needed': 'true'}


def finish(directory, repository, number, commit, tag):
    root = f'https://api.github.com/repos/{repository}'
    marker = identity(number, commit)
    validate_tag(tag)
    verify_merge(root, repository, number, commit)
    info = json.loads((directory / 'build-info.json').read_text())
    if info['tag'] != tag or info['commit'] != commit:
        raise ValueError('Firmware does not match the selected merge and version')
    release = find_release(listing(root, 'releases'), marker)
    if release and (release['tag_name'] != tag or release['target_commitish'] != commit):
        raise ValueError('Existing release identity changed')
    # Never repurpose a manual release, even if it happens to use the same commit.
    if not release and any(r['tag_name'] == tag for r in listing(root, 'releases')):
        raise ValueError('Version already belongs to another release')
    reserve_tag(root, tag, marker, commit)
    if not release:
        release = api(f'{root}/releases', json.dumps({
            'tag_name': tag, 'target_commitish': commit, 'name': tag,
            'body': marker + f'\n\nAutomatikus firmware-kiadás a #{number} PR main ágra történő összevonásából.',
            'draft': True, 'prerelease': False, 'generate_release_notes': True
        }).encode())
    publish(directory, repository, tag, commit, release['id'], allow_draft=True)
    if release['draft']:
        result = api(f'{root}/releases/{release["id"]}',
                     json.dumps({'draft': False}).encode(), method='PATCH')
        if result['draft']:
            raise ValueError('Release was not published')
    print(f'Release ready: {tag}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'finish'])
    parser.add_argument('--repository', required=True)
    parser.add_argument('--pr', type=int, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--tag')
    parser.add_argument('--directory', type=Path, default=Path('dist'))
    args = parser.parse_args()
    if args.action == 'prepare':
        result = prepare(args.repository, args.pr, args.commit)
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
            for key, value in result.items():
                output.write(f'{key}={value}\n')
        print(json.dumps(result))
    else:
        if not args.tag:
            parser.error('--tag is required for finish')
        finish(args.directory, args.repository, args.pr, args.commit, args.tag)
