#!/usr/bin/env python3
"""Select one build/version path for PR checks, main pushes and manual releases."""
import argparse
import json
import os
import re

from auto_release import listing, prepare
from version import validate_tag


def plan(repository, commit, event, ref, tag='', automatic=False):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Invalid repository')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Invalid source commit')
    result = {'tag': validate_tag(tag or 'v0.0.0-ci'), 'pr': '0',
              'publish': 'false', 'build': 'true'}
    if not automatic:
        return result
    # Kiadást csak a védett main esemény választhat; PR, kézi indítás és más
    # ág akkor sem publikálhat, ha tévesen automatic=true bemenetet kapna.
    if event != 'push' or ref != 'refs/heads/main' or tag:
        raise ValueError('Automatic release requires an unversioned main push')
    root = f'https://api.github.com/repos/{repository}'
    associated = listing(root, f'commits/{commit}/pulls')
    matches = []
    for pr in associated:
        base = pr.get('base') or {}
        if (pr.get('merged_at') and pr.get('merge_commit_sha') == commit and
                base.get('ref') == 'main' and
                (base.get('repo') or {}).get('full_name') == repository):
            matches.append(pr['number'])
    if len(matches) > 1:
        raise ValueError('Multiple merged main PRs claim the source commit')
    if not matches:
        # Közvetlen main push: teljes CI, de nincs új tag vagy release.
        return result
    number = matches[0]
    selected = prepare(repository, number, commit)
    result.update(tag=selected['tag'], pr=str(number), publish=selected['needed'],
                  build=selected['needed'])
    # Egy már publikált merge újrafuttatása nem fordít új binárist ugyanahhoz
    # a verzióhoz; a korábbi csomag és a meglévő kiadás változatlan marad.
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repository', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--event', required=True)
    parser.add_argument('--ref', required=True)
    parser.add_argument('--tag', default='')
    parser.add_argument('--automatic', choices=['true', 'false'], default='false')
    args = parser.parse_args()
    result = plan(args.repository, args.commit, args.event, args.ref, args.tag,
                  args.automatic == 'true')
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        for key, value in result.items():
            output.write(f'{key}={value}\n')
    print(json.dumps(result))
